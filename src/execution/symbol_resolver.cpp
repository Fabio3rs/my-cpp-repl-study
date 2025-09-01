#include "execution/symbol_resolver.hpp"
#include "../repl.hpp"
#include "../utility/file_raii.hpp"
#include "utility/library_introspection.hpp"
#include <cstdio>
#include <dlfcn.h>
#include <format>
#include <fstream>
#include <iostream>

// Forward declarations for functions defined in repl.cpp
extern int onlyBuildLib(std::string compiler, const std::string &name,
                        std::string ext = ".cpp", std::string std = "gnu++20",
                        std::string_view extra_args = {});

namespace execution {

// Configuração global para wrappers (necessária para callbacks do assembly)
static SymbolResolver::WrapperConfig *g_globalConfig = nullptr;

std::string SymbolResolver::generateFunctionWrapper(const VarDecl &fnvars) {
    std::string wrapper;

    auto qualTypestr = std::string(fnvars.qualType);
    auto parem = qualTypestr.find_first_of('(');

    // Gera declaração da função naked
    qualTypestr =
        std::format("void __attribute__ ((naked)) {}()", fnvars.mangledName);

    /*
     * Este é o trampoline que será chamado quando a biblioteca
     * inicializa e as funções ainda não estão prontas. Ele tentará
     * carregar o endereço da função e definir o ponteiro.
     */
    wrapper += std::format("static void __attribute__((naked)) loadFn_{}();\n",
                           fnvars.mangledName);

    wrapper += std::format("void *{}_ptr = "
                           "(void*)(loadFn_{});\n\n",
                           fnvars.mangledName, fnvars.mangledName);

    wrapper += qualTypestr + " {\n";
    wrapper += std::format(R"(    __asm__ __volatile__ (
        "jmp *%0\n"
        :
        : "r" ({}_ptr)
    );
}}
)",
                           fnvars.mangledName);

    wrapper +=
        std::format("static void __attribute__((naked)) loadFn_{}() {{\n",
                    fnvars.mangledName);

    /*
     * Observação: Mantém o stack alinhado em 16 bytes antes de chamar uma
     * função
     */
    wrapper += R"(
    __asm__(
        // Save all general-purpose registers
        "pushq   %rax                \n"
        "pushq   %rbx                \n"
        "pushq   %rcx                \n"
        "pushq   %rdx                \n"
        "pushq   %rsi                \n"
        "pushq   %rdi                \n"
        "pushq   %rbp                \n"
        "pushq   %r8                 \n"
        "pushq   %r9                 \n"
        "pushq   %r10                \n"
        "pushq   %r11                \n"
        "pushq   %r12                \n"
        "pushq   %r13                \n"
        "pushq   %r14                \n"
        "pushq   %r15                \n"
        "movq    %rsp, %rbp          \n" // Set Base Pointer
    );
        // Push parameters onto the stack
    __asm__ __volatile__ (
        "movq %0, %%rax"
        :
        : "r" (&)" +
               std::format("{}_ptr", fnvars.mangledName) +
               R"()
    );

    __asm__(
        // Push parameters onto the stack
        "movq    %rax, %rdi          \n" // Parameter 1: pointer address
        "leaq    .LC)" +
               std::format("{}", fnvars.mangledName) +
               R"((%rip), %rsi    \n" // Address of string

        // Call loadfnToPtr function
        "call    loadfnToPtr  \n" // Call loadfnToPtr function

        // Restore all general-purpose registers
        "popq    %r15                \n"
        "popq    %r14                \n"
        "popq    %r13                \n"
        "popq    %r12                \n"
        "popq    %r11                \n"
        "popq    %r10                \n"
        "popq    %r9                 \n"
        "popq    %r8                 \n"
        "popq    %rbp                \n"
        "popq    %rdi                \n"
        "popq    %rsi                \n"
        "popq    %rdx                \n"
        "popq    %rcx                \n"
        "popq    %rbx                \n"
        "popq    %rax                \n");
    __asm__ __volatile__("jmp *%0\n"
                         :
                         : "r"()" +
               fnvars.mangledName +
               R"(_ptr));

    __asm__(".section .rodata            \n"
            ".LC)" +
               fnvars.mangledName +
               R"(:                        \n"
            ".string \")" +
               fnvars.mangledName +
               R"(\"                \n");
    __asm__(".section .text            \n");
}
            )";

    return wrapper;
}

std::unordered_map<std::string, std::string>
SymbolResolver::prepareFunctionWrapper(
    const std::string &name, const std::vector<VarDecl> &vars,
    WrapperConfig &config,
    const std::unordered_set<std::string> &existingFunctions) {

    std::string wrapperCode;
    std::unordered_map<std::string, std::string> functions;
    std::unordered_set<std::string> addedFns;

    for (const auto &fnvars : vars) {
        std::cout << fnvars.name << std::endl;

        if (fnvars.kind != "FunctionDecl" && fnvars.kind != "CXXMethodDecl" &&
            fnvars.kind != "CXXConstructorDecl") {
            continue;
        }

        if (fnvars.mangledName == "main") {
            continue;
        }

        if (addedFns.contains(fnvars.mangledName)) {
            continue;
        }

        if (!existingFunctions.contains(fnvars.mangledName)) {
            addedFns.insert(fnvars.mangledName);
            wrapperCode += generateFunctionWrapper(fnvars);
        }

        functions[fnvars.mangledName] = fnvars.name;
    }

    if (!functions.empty()) {
        auto wrappername = std::format("wrapper_{}", name);
        std::fstream wrapperOutput(std::format("{}.c", wrappername),
                                   std::ios::out | std::ios::trunc);
        wrapperOutput << wrapperCode << std::endl;
        wrapperOutput.close();

        // Compilar wrapper
        int result =
            onlyBuildLib("clang", wrappername, ".c", "c11", config.extraArgs);
        (void)result; // Suprimir warning de variável não utilizada
    }

    return functions;
}

void SymbolResolver::fillWrapperPtrs(
    const std::unordered_map<std::string, std::string> &functions,
    void *handlewp, void *handle, WrapperConfig &config) {

    for (const auto &[mangledName, fns] : functions) {
        void *fnptr = dlsym(handle, mangledName.c_str());

        if (!fnptr) {
            void **wrap_ptrfn =
                (void **)dlsym(handlewp, std::format("{}_ptr", fns).c_str());

            if (!wrap_ptrfn) {
                continue;
            }

            auto it = config.functionWrappers.find(mangledName);
            if (it == config.functionWrappers.end()) {
                continue;
            }

            it->second.fnptr = nullptr;
            it->second.wrap_ptrfn = wrap_ptrfn;
            continue;
        }

        auto it = config.functionWrappers.find(mangledName);
        if (it != config.functionWrappers.end()) {
            auto &fnWrapper = it->second;
            fnWrapper.fnptr = fnptr;

            void **wrap_ptrfn = fnWrapper.wrap_ptrfn;
            if (wrap_ptrfn) {
                *wrap_ptrfn = fnptr;
            }
            continue;
        }

        // Criar nova entrada se não existir
        auto &fn = config.functionWrappers[mangledName];
        fn.fnptr = fnptr;

        void **wrap_ptrfn = (void **)dlsym(
            handlewp, std::format("{}_ptr", mangledName).c_str());

        if (!wrap_ptrfn) {
            std::cerr << std::format("Cannot load symbol '{}': {}\n",
                                     mangledName, dlerror());
            continue;
        }

        *wrap_ptrfn = fnptr;
        fn.wrap_ptrfn = wrap_ptrfn;
    }
}

std::unordered_map<std::string, uintptr_t>
SymbolResolver::resolveSymbolOffsetsFromLibraryFile(
    const std::unordered_map<std::string, std::string> &functions,
    const std::string &libraryPath) {

    std::unordered_map<std::string, uintptr_t> symbolOffsets;

    if (functions.empty()) {
        return symbolOffsets;
    }

    auto command = std::format("nm -D --defined-only {}", libraryPath);
    auto symbol_address_command = utility::make_popen(command, "r");
    if (!symbol_address_command) {
        perror("popen");
        return symbolOffsets;
    }

    char line[1024]{};

    while (fgets(line, 1024, symbol_address_command.get()) != NULL) {
        uintptr_t symbol_offset = 0;
        char symbol_name[256];
        sscanf(line, "%zx %*s %255s", &symbol_offset, symbol_name);

        if (functions.contains(symbol_name)) {
            symbolOffsets[symbol_name] = symbol_offset;
        }
    }

    return symbolOffsets;
}

void SymbolResolver::loadSymbolToPtr(void **ptr, const char *name,
                                     const WrapperConfig &config) {
    std::cout << __LINE__ << ": Function segfaulted: " << name
              << "   library: " << config.libraryPath << std::endl;

    /*
     * TODO: Test again with dlopen RTLD_NOLOAD and dlsym
     */
    auto base = utility::getLibraryStartAddress(config.libraryPath.c_str());

    if (base == 0) {
        std::cerr << __LINE__ << ": base == 0" << config.libraryPath
                  << std::endl;

        auto handle = dlopen(config.libraryPath.c_str(), RTLD_NOLOAD);

        if (handle == nullptr) {
            std::cerr << __LINE__ << ": handle == nullptr" << config.libraryPath
                      << std::endl;
            return;
        }

        for (const auto &[symbol, offset] : config.symbolOffsets) {
            void **wrap_ptrfn =
                (void **)dlsym(RTLD_DEFAULT, (symbol + "_ptr").c_str());

            if (wrap_ptrfn == nullptr) {
                continue;
            }

            auto tmp = dlsym(handle, symbol.c_str());

            if (tmp == nullptr) {
                std::cerr << __LINE__ << ": tmp == nullptr"
                          << config.libraryPath << std::endl;
                continue;
            }

            *wrap_ptrfn = tmp;
        }

        if (*ptr == nullptr) {
            *ptr = dlsym(handle, name);
        }
        return;
    }

    for (const auto &[symbol, offset] : config.symbolOffsets) {
        void **wrap_ptrfn =
            (void **)dlsym(RTLD_DEFAULT, (symbol + "_ptr").c_str());

        if (wrap_ptrfn == nullptr) {
            continue;
        }

        *wrap_ptrfn = reinterpret_cast<void *>(base + offset);
    }
}

// Callback C para interface com assembly
extern "C" void loadfnToPtr(void **ptr, const char *name) {
    if (g_globalConfig) {
        SymbolResolver::loadSymbolToPtr(ptr, name, *g_globalConfig);
    } else {
        std::cerr << std::format("Error: No global wrapper configuration "
                                 "available for symbol: {}",
                                 name)
                  << std::endl;
        assert(false);
    }
}

// Função para definir configuração global
void SymbolResolver::setGlobalWrapperConfig(WrapperConfig *config) {
    g_globalConfig = config;
}

} // namespace execution
