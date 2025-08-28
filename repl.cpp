#include "stdafx.hpp"

#include "backtraced_exceptions.hpp"
#include "printerOverloads.hpp"

#include "analysis/ast_analyzer.hpp"
#include "analysis/ast_context.hpp"
#include "analysis/clang_ast_adapter.hpp"
#include "compiler/compiler_service.hpp"
#include "execution/execution_engine.hpp"
#include "repl.hpp"
#include "simdjson.h"
#include "utility/assembly_info.hpp"
#include "utility/file_raii.hpp"
#include "utility/library_introspection.hpp"
#include "utility/quote.hpp"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <exceptdefs.h>
#include <execinfo.h>
#include <execution>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <mutex>
#include <readline/history.h>
#include <readline/readline.h>
#include <regex>
#include <segvcatch.h>
#include <string>
#include <string_view>
#include <tuple>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

static BuildSettings buildSettings;
static ReplState replState;
int verbosityLevel = 0; // Default to quiet mode (errors only)

// Callback para merge de variáveis no CompilerService
static void mergeVarsCallback(const std::vector<VarDecl> &vars) {
    for (const auto &var : vars) {
        if (replState.varsNames.find(var.name) == replState.varsNames.end()) {
            replState.varsNames.insert(var.name);
            replState.allTheVariables.push_back(var);
        }
    }
}

// Instância global do CompilerService
static std::unique_ptr<compiler::CompilerService> compilerService;

// Inicializar o CompilerService
static void initCompilerService() {
    if (!compilerService) {
        compilerService = std::make_unique<compiler::CompilerService>(
            &buildSettings,
            nullptr, // AstContext será definido quando necessário
            mergeVarsCallback);
    }
}

std::any lastReplResult; // used for external commands

// Forward declaration for command handler usage
bool loadPrebuilt(const std::string &path);

// Forward declaration for functions that still exist
void savePrintAllVarsLibrary(const std::vector<VarDecl> &vars);

// Forward declaration of adapter
// Adapter defined below

#include "commands/repl_commands.hpp"

static inline bool handleReplCommand(std::string_view line, BuildSettings &bs,
                                     bool &useCpp2) {
    repl_commands::ReplCtxView view{
        .includeDirectories = &bs.includeDirectories,
        .preprocessorDefinitions = &bs.preprocessorDefinitions,
        .linkLibraries = &bs.linkLibraries,
        .useCpp2Ptr = &useCpp2};
    return repl_commands::handleReplCommand(line, view);
}

// A classe ClangAstAnalyzerAdapter foi movida para
// include/analysis/clang_ast_adapter.hpp

int onlyBuildLib(std::string compiler, const std::string &name,
                 std::string ext = ".cpp", std::string std = "gnu++20",
                 std::string_view extra_args = {}) {
    initCompilerService();

    auto result =
        compilerService->buildLibraryOnly(compiler, name, ext, std, extra_args);

    if (!result) {
        std::cerr << std::format("Build failed with error code: {}\n",
                                 static_cast<int>(result.error));
    }

    return result.success() ? result.value : -1;
}

int buildLibAndDumpAST(std::string compiler, const std::string &name,
                       std::string ext = ".cpp", std::string std = "gnu++20") {
    initCompilerService();

    auto result =
        compilerService->buildLibraryWithAST(compiler, name, ext, std);

    if (!result) {
        std::cerr << std::format("Build with AST failed with error code: {}\n",
                                 static_cast<int>(result.error));
        return -1;
    }

    // As variáveis já foram merged via callback, agora só precisamos salvar a
    // biblioteca de print
    savePrintAllVarsLibrary(result.value);

    return 0;
}

// Removidas as variáveis globais outputHeader e includedFiles
// Agora encapsuladas na classe AstContext

static std::mutex varsWriteMutex;

// Funções analyzeInnerAST, analyzeASTFromJsonString e analyzeASTFile
// foram movidas para analysis::ContextualAstAnalyzer

auto runProgramGetOutput(std::string_view cmd) {
    std::pair<std::string, int> result{{}, -1};

    auto pipe = utility::make_popen(cmd.data(), "r");

    if (!pipe) {
        std::cerr << "popen() failed!" << std::endl;
        return result;
    }

    constexpr size_t MBPAGE2 = 1024 * 1024 * 2;

    auto &buffer = result.first;
    try {
        buffer.resize(MBPAGE2);
    } catch (const std::bad_alloc &e) {
        pipe.reset();
        std::cerr << "bad_alloc caught: " << e.what() << std::endl;
        return result;
    }

    size_t finalSize = 0;

    while (!feof(pipe.get())) {
        size_t read = fread(buffer.data() + finalSize, 1,
                            buffer.size() - finalSize, pipe.get());

        if (read < 0) {
            std::cerr << "fread() failed!" << std::endl;
            break;
        }

        finalSize += read;

        try {
            if (finalSize == buffer.size()) {
                buffer.resize(buffer.size() * 2);
            }
        } catch (const std::bad_alloc &e) {
            std::cerr << "bad_alloc caught: " << e.what() << std::endl;
            break;
        }
    }

    pipe.reset();
    result.second = pipe.get_deleter().retcode;

    if (result.second != 0) {
        std::cerr << std::format("program failed! {}\n", result.second);
    }

    buffer.resize(finalSize);

    return result;
}

int build_precompiledheader(
    std::string compiler = "clang++",
    std::shared_ptr<analysis::AstContext> context = nullptr) {
    initCompilerService();

    auto result = compilerService->buildPrecompiledHeader(compiler, context);

    if (!result) {
        std::cerr << std::format(
            "Precompiled header build failed with error code: {}\n",
            static_cast<int>(result.error));
        return -1;
    }

    return 0;
}

// moved into replState

void printPrepareAllSave(const std::vector<VarDecl> &vars) {
    if (vars.empty()) {
        return;
    }

    static int i = 0;
    std::string name = std::format("printerOutput{}", i++);

    std::fstream printerOutput(std::format("{}.cpp", name),
                               std::ios::out | std::ios::trunc);

    printerOutput << "#include \"printerOutput.hpp\"\n\n" << std::endl;
    printerOutput << "#include \"decl_amalgama.hpp\"\n\n" << std::endl;

    for (const auto &var : vars) {
        // std::cout << __LINE__ << var.kind << std::endl;
        /*std::cout << var.qualType << "   " << var.type << "   " << var.name
                  << std::endl;*/
        if (var.kind == "VarDecl") {
            printerOutput << std::format("extern \"C\" void printvar_{}() {{\n",
                                         var.name);
            printerOutput << std::format("  printdata({}, \"{}\", \"{}\");\n",
                                         var.name, var.name, var.qualType);
            printerOutput << "}\n";
        }
    }

    printerOutput << "void printall() {\n";

    for (const auto &var : vars) {
        // std::cout << __LINE__ << var.kind << std::endl;
        if (var.kind == "VarDecl") {
            printerOutput << std::format("printdata({}, \"{}\", \"{}\");\n",
                                         var.name, var.name, var.qualType);
        }
    }

    printerOutput << "}\n";

    printerOutput.close();

    int buildLibRes = onlyBuildLib("clang++", name);

    if (buildLibRes != 0) {
        std::cerr << std::format("buildLibRes != 0: {}\n", name);
        return;
    }

    void *handlep =
        dlopen(std::format("./lib{}.so", name).c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!handlep) {
        std::cerr << std::format("Cannot open library: {}\n", dlerror());
        return;
    }

    for (const auto &var : vars) {
        // std::cout << __LINE__ << var.kind << std::endl;
        if (var.kind == "VarDecl") {
            void (*printvar)() = (void (*)())dlsym(
                handlep, std::format("printvar_{}", var.name).c_str());
            if (!printvar) {
                std::cerr << std::format(
                    "Cannot load symbol 'printvar_{}': {}\n", var.name,
                    dlerror());
                dlclose(handlep);
                return;
            }

            replState.varPrinterAddresses[var.name] = printvar;
        }
    }
}

void savePrintAllVarsLibrary(const std::vector<VarDecl> &vars) {
    if (vars.empty()) {
        return;
    }

    std::fstream printerOutput("printerOutput.cpp",
                               std::ios::out | std::ios::trunc);

    printerOutput << "#include \"printerOutput.hpp\"\n\n" << std::endl;
    printerOutput << "#include \"decl_amalgama.hpp\"\n\n" << std::endl;

    printerOutput << "void printall() {\n";

    for (const auto &var : vars) {
        // std::cout << __LINE__ << var.kind << std::endl;
        if (var.kind == "VarDecl") {
            printerOutput << std::format("printdata({}, \"{}\", \"{}\");\n",
                                         var.name, var.name, var.qualType);
        }
    }

    printerOutput << "}\n";

    printerOutput.close();

    onlyBuildLib("clang++", "printerOutput");
}

struct wrapperFn {
    void *fnptr;
    void **wrap_ptrfn;
};

// MOVED: fnNames moved to execution::GlobalExecutionState

void mergeVars(const std::vector<VarDecl> &vars) {
    for (const auto &var : vars) {
        if (replState.varsNames.find(var.name) == replState.varsNames.end()) {
            replState.varsNames.insert(var.name);
            replState.allTheVariables.push_back(var);
        }
    }
}

auto analyzeCustomCommands(
    const std::unordered_map<std::string, std::string> &commands)
    -> std::pair<std::vector<VarDecl>, int> {
    std::pair<std::vector<VarDecl>, int> resultc;

    // Garante que o CompilerService está inicializado
    initCompilerService();

    // Converte o mapa de comandos para um vetor de comandos formatados
    std::vector<std::string> formattedCommands;

    for (const auto &namecmd : commands) {
        if (namecmd.first.empty()) {
            continue;
        }

        const auto path = std::filesystem::path(namecmd.first);
        std::string purefilename = path.filename().string();
        purefilename = purefilename.substr(0, purefilename.find_last_of('.'));

        std::string logname = std::format("{}.log", purefilename);
        std::string cmd = namecmd.second;

        // Adiciona flags padrão se necessário
        if ((namecmd.second.find("-std=gnu++20") != std::string::npos ||
             namecmd.second.find("-std=") == std::string::npos) &&
            namecmd.second.find("-fvisibility=hidden") == std::string::npos) {
            cmd += " -std=gnu++20 -include precompiledheader.hpp";
        }

        // Adiciona flags para análise AST e redirecionamento para JSON
        std::string jsonFile = std::format("{}_ast.json", purefilename);
        cmd += " -Xclang -ast-dump=json -fsyntax-only";
        cmd += std::format(" 2>{} > {}", logname, jsonFile);

        formattedCommands.push_back(cmd);
    }

    // Usa o CompilerService para analisar os comandos
    auto result = compilerService->analyzeCustomCommands(formattedCommands);

    if (!result.success()) {
        std::cerr << "Erro na análise customizada usando CompilerService"
                  << std::endl;
        resultc.second = 1;
        return resultc;
    }

    // Converte as strings encontradas em VarDecls
    // (Nota: As variáveis já foram mescladas pelo callback mergeVars)
    // Aqui só precisamos definir o código de retorno
    resultc.second = 0;

    // Como o callback já foi chamado, as variáveis já estão em
    // replState.allTheVariables Mas vamos retornar as que foram encontradas
    // nesta análise específica
    std::vector<VarDecl> foundVars;
    for (const auto &varName : result.value) {
        VarDecl decl;
        decl.name = varName;
        decl.type = "auto"; // Tipo genérico
        foundVars.push_back(decl);
    }

    resultc.first = std::move(foundVars);
    return resultc;
}

auto linkAllObjects(const std::vector<std::string> &objects,
                    const std::string &libname) -> int {
    initCompilerService();

    auto result = compilerService->linkObjects(objects, libname);

    if (!result) {
        std::cerr << std::format("Linking failed with error code: {}\n",
                                 static_cast<int>(result.error));
    }

    return result.success() ? result.value : -1;
}

auto buildLibAndDumpASTWithoutPrint(
    std::string compiler, const std::string &libname,
    const std::vector<std::string> &names,
    const std::string &std) -> std::pair<std::vector<VarDecl>, int> {
    initCompilerService();

    auto result = compilerService->buildMultipleSourcesWithAST(
        compiler, libname, names, std);

    if (!result) {
        std::cerr << std::format(
            "Build multiple sources failed with error code: {}\n",
            static_cast<int>(result.error));
        return {{}, -1};
    }

    // As variáveis já foram merged via callback
    return {result.value.variables, result.value.returnCode};
}

int runPrintAll() {
    void *handlep = dlopen("./libprinterOutput.so", RTLD_NOW | RTLD_GLOBAL);
    if (!handlep) {
        std::cerr << std::format("Cannot open library: {}\n", dlerror());
        return EXIT_FAILURE;
    }

    void (*printall)() = (void (*)())dlsym(handlep, "_Z8printallv");
    if (!printall) {
        std::cerr << std::format("Cannot load symbol 'printall': {}\n",
                                 dlerror());
        dlclose(handlep);
        return EXIT_FAILURE;
    }

    printall();

    dlclose(handlep);

    return EXIT_SUCCESS;
}

#define MAX_LINE_LENGTH 1024

/*
 * Added to [try to] solve the problem of the function not being loaded when the
 * library initializes
 */
// MOVED: lastLibrary and symbolsToResolve moved to
// execution::GlobalExecutionState

// Redirecionamento da função global obrigatória para o ExecutionEngine
extern "C" void loadfnToPtr(void **ptr, const char *name) {
    /*
     * Function related to loadFn_"funcion name", this function is called when
     * the library constructor tries to run and the function is not loaded yet.
     */
    auto &state = execution::getGlobalExecutionState();

    std::cout << __LINE__ << ": Function segfaulted: " << name
              << "   library: " << state.lastLibrary << std::endl;

    /*
     * TODO: Test again with dlopen RTLD_NOLOAD and dlsym
     */
    auto base = utility::getLibraryStartAddress(state.lastLibrary.c_str());

    if (base == 0) {
        std::cerr << __LINE__ << ": base == 0" << state.lastLibrary
                  << std::endl;

        auto handle = dlopen(state.lastLibrary.c_str(), RTLD_NOLOAD);

        if (handle == nullptr) {
            std::cerr << __LINE__ << ": handle == nullptr" << state.lastLibrary
                      << std::endl;
            return;
        }

        for (const auto &[symbol, offset] : state.symbolsToResolve) {
            void **wrap_ptrfn =
                (void **)dlsym(RTLD_DEFAULT, (symbol + "_ptr").c_str());

            if (wrap_ptrfn == nullptr) {
                continue;
            }

            auto tmp = dlsym(handle, symbol.c_str());

            if (tmp == nullptr) {
                std::cerr << __LINE__ << ": tmp == nullptr" << state.lastLibrary
                          << std::endl;
                continue;
            }

            *wrap_ptrfn = tmp;
        }

        if (*ptr == nullptr) {
            *ptr = dlsym(handle, name);
        }
        return;
    }

    for (const auto &[symbol, offset] : state.symbolsToResolve) {
        void **wrap_ptrfn =
            (void **)dlsym(RTLD_DEFAULT, (symbol + "_ptr").c_str());

        if (wrap_ptrfn == nullptr) {
            continue;
        }

        *wrap_ptrfn = reinterpret_cast<void *>(base + offset);
    }
}

void generateFunctionWrapper(std::string &wrapper, const VarDecl &fnvars) {
    auto qualTypestr = std::string(fnvars.qualType);
    auto parem = qualTypestr.find_first_of('(');

    // if (parem == std::string::npos || fnvars.kind != "FunctionDecl")
    // {
    qualTypestr = std::format("extern \"C\" void __attribute__ ((naked)) {}()",
                              fnvars.mangledName);
    /*} else {
        qualTypestr.insert(parem,
                           std::string(" __attribute__ ((naked)) " +
                                       fnvars.mangledName));
        qualTypestr.insert(0, "extern \"C\" ");
    }*/

    /*
     * This is the function that will be called when the library
     * initializes and the functions are not ready yet, it will try to
     * load the function address and set it to the pointer
     */
    wrapper += std::format("static void __attribute__((naked)) loadFn_{}();\n",
                           fnvars.mangledName);

    wrapper += std::format("extern \"C\" void *{}_ptr = "
                           "reinterpret_cast<void*>(loadFn_{});\n\n",
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
     * Observations: Keep the stack aligned in 16 bytes before calling a
     * function
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
        //"leaq    _Z21qRegisterResourceDataiPKhS0_S0__ptr(%rip), %rax  \n" // Address of pointer
    __asm__ __volatile__ (
        "movq %0, %%rax"
        :
        : "r" (&)" +
               std::format("{}_ptr", fnvars.mangledName) +
               R"()
    );

    __asm__(
        // Push parameters onto the stack
        //"leaq    )" +
               fnvars.mangledName +
               R"(_ptr(%rip), %rax  \n" // Address
                                                                          // of
                                                                          // pointer
        "movq    %rax, %rdi          \n" // Parameter 1: pointer address
        "leaq    .LC)" +
               std::format("{}", fnvars.mangledName) +
               R"((%rip), %rsi    \n" // Address of string "a"

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
}

void prepareFunctionWrapper(
    const std::string &name, const std::vector<VarDecl> &vars,
    std::unordered_map<std::string, std::string> &functions) {
    std::string wrapper;

    // wrapper += "#include \"decl_amalgama.hpp\"\n\n";

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

        if (!execution::getGlobalExecutionState().hasFnName(
                fnvars.mangledName)) {
            addedFns.insert(fnvars.mangledName);

            generateFunctionWrapper(wrapper, fnvars);
        }

        functions[fnvars.mangledName] = fnvars.name;
    }

    if (!functions.empty()) {
        auto wrappername = std::format("wrapper_{}", name);
        std::fstream wrapperOutput(std::format("{}.cpp", wrappername),
                                   std::ios::out | std::ios::trunc);
        wrapperOutput << wrapper << std::endl;
        wrapperOutput.close();

        onlyBuildLib("clang++", wrappername, ".cpp", "gnu++20", "-nostdlib");
    }
}

void fillWrapperPtrs(
    const std::unordered_map<std::string, std::string> &functions,
    void *handlewp, void *handle) {
    for (const auto &[mangledName, fns] : functions) {
        void *fnptr = dlsym(handle, mangledName.c_str());
        if (!fnptr) {
            void **wrap_ptrfn =
                (void **)dlsym(handlewp, std::format("{}_ptr", fns).c_str());

            if (!wrap_ptrfn) {
                continue;
            }

            if (!execution::getGlobalExecutionState().hasFnName(mangledName)) {
                continue;
            }

            auto &fn =
                execution::getGlobalExecutionState().getFnName(mangledName);
            fn.fnptr = nullptr;
            fn.wrap_ptrfn = wrap_ptrfn;
            continue;
        }

        if (execution::getGlobalExecutionState().hasFnName(mangledName)) {
            auto &fnWrapper =
                execution::getGlobalExecutionState().getFnName(mangledName);
            fnWrapper.fnptr = fnptr;

            void **wrap_ptrfn = fnWrapper.wrap_ptrfn;

            if (wrap_ptrfn) {
                *wrap_ptrfn = fnptr;
            }
            continue;
        }

        auto &fn = execution::getGlobalExecutionState().getFnName(mangledName);

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

// moved into replState

void evalEverything() {
    for (const auto &fn : replState.lazyEvalFns) {
        fn();
    }

    replState.lazyEvalFns.clear();
}

void resolveSymbolOffsetsFromLibraryFile(
    const std::unordered_map<std::string, std::string> &functions) {
    if (functions.empty()) {
        return;
    }

    auto command =
        std::format("nm -D --defined-only {}",
                    execution::getGlobalExecutionState().getLastLibrary());
    auto symbol_address_command = utility::make_popen(command, "r");
    if (!symbol_address_command) {
        perror("popen");
        exit(EXIT_FAILURE);
    }

    char line[1024]{};

    while (fgets(line, 1024, symbol_address_command.get()) != NULL) {
        uintptr_t symbol_offset = 0;
        char symbol_name[256];
        sscanf(line, "%zx %*s %255s", &symbol_offset, symbol_name);

        if (functions.contains(symbol_name)) {
            execution::getGlobalExecutionState().addSymbolToResolve(
                symbol_name, symbol_offset);
        }
    }
}

auto prepareWraperAndLoadCodeLib(const CompilerCodeCfg &cfg,
                                 std::vector<VarDecl> &&vars) -> EvalResult {
    std::unordered_map<std::string, std::string> functions;

    prepareFunctionWrapper(cfg.repl_name, vars, functions);

    void *handlewp = nullptr;

    if (!functions.empty()) {
        handlewp =
            dlopen(std::format("./libwrapper_{}.so", cfg.repl_name).c_str(),
                   RTLD_NOW | RTLD_GLOBAL);

        if (!handlewp) {
            std::cerr << std::format("Cannot wrapper library: {}\n", dlerror());
            // return false;
        }
    }

    int dlOpenFlags = RTLD_NOW | RTLD_GLOBAL;

    if (cfg.lazyEval) {
        std::cout << "lazyEval:  " << cfg.repl_name << std::endl;
        dlOpenFlags = RTLD_LAZY | RTLD_GLOBAL;
    }

    auto load_start = std::chrono::steady_clock::now();

    auto libraryPath = std::format("./lib{}.so", cfg.repl_name);
    execution::getGlobalExecutionState().setLastLibrary(libraryPath);

    resolveSymbolOffsetsFromLibraryFile(functions);

    EvalResult result;
    result.libpath = libraryPath;

    void *handle = dlopen(libraryPath.c_str(), dlOpenFlags);
    result.handle = handle;
    if (!handle) {
        std::cerr << __FILE__ << ":" << __LINE__
                  << " Cannot open library: " << dlerror() << '\n';
        result.success = false;
        return result;
    }

    execution::getGlobalExecutionState().clearSymbolsToResolve();

    auto load_end = std::chrono::steady_clock::now();

    std::cout << "load time: "
              << std::chrono::duration_cast<std::chrono::microseconds>(
                     load_end - load_start)
                     .count()
              << "us" << std::endl;

    auto eval = [functions = std::move(functions), handlewp = handlewp,
                 handle = handle, vars = std::move(vars)]() mutable {
        fillWrapperPtrs(functions, handlewp, handle);

        printPrepareAllSave(vars);

        void (*execv)() = (void (*)())dlsym(handle, "_Z4execv");
        if (execv || (execv = (void (*)())dlsym(handle, "exec"))) {
            auto exec_start = std::chrono::steady_clock::now();
            try {
                execv();
            } catch (const segvcatch::hardware_exception &e) {
                std::cerr << "Hardware exception: " << e.what() << std::endl;
                std::cerr << assembly_info::getInstructionAndSource(
                                 getpid(),
                                 reinterpret_cast<uintptr_t>(e.info.addr))
                          << std::endl;
                auto [btrace, size] =
                    backtraced_exceptions::get_backtrace_for(e);

                if (btrace != nullptr && size > 0) {
                    std::cerr
                        << "Backtrace (based on callstack return address):\n";
                    backtraced_exceptions::print_backtrace(btrace, size);
                } else {
                    std::cerr << "Backtrace not available\n";
                }
            } catch (const std::exception &e) {
                std::cerr << std::format("C++ exception on exec/eval: {}\n",
                                         e.what());

                auto [btrace, size] =
                    backtraced_exceptions::get_backtrace_for(e);

                if (btrace != nullptr && size > 0) {
                    std::cerr
                        << "Backtrace (based on callstack return address):\n";
                    backtraced_exceptions::print_backtrace(btrace, size);
                } else {
                    std::cerr << "Backtrace not available\n";
                }
            } catch (...) {
                std::cerr << "Unknown C++ exception on exec/eval\n";
            }

            auto exec_end = std::chrono::steady_clock::now();

            std::cout << "exec time: "
                      << std::chrono::duration_cast<std::chrono::microseconds>(
                             exec_end - exec_start)
                             .count()
                      << "us" << std::endl;
        }

        for (const auto &var : vars) {
            if (var.kind != "VarDecl") {
                continue;
            }

            auto it = replState.varPrinterAddresses.find(var.name);

            if (it != replState.varPrinterAddresses.end()) {
                it->second();
            } else {
                std::cout << "not found: " << var.name << std::endl;
            }
        }

        std::cout << std::endl;

        return true;
    };

    if (auto execPtr = (void (*)())dlsym(handle, "_Z4execv");
        execPtr != nullptr) {
        result.exec = execPtr;
    }

    if (cfg.lazyEval) {
        replState.lazyEvalFns.push_back(eval);
    } else {
        eval();
    }

    result.success = true;
    return result;
}

bool loadPrebuilt(const std::string &path) {
    std::unordered_map<std::string, std::string> functions;

    std::vector<VarDecl> vars = utility::getBuiltFileDecls(path);

    vars.erase(std::remove_if(vars.begin(), vars.end(),
                              [](const VarDecl &var) {
                                  return (var.name == "_init" ||
                                          var.name == "_fini") ||
                                         (var.mangledName == "_init" ||
                                          var.mangledName == "_fini");
                              }),
               vars.end());

    auto filename = "lib_" + std::filesystem::path(path).filename().string();

    prepareFunctionWrapper(filename, vars, functions);

    void *handlewp = nullptr;

    if (!functions.empty()) {
        handlewp = dlopen(std::format("./libwrapper_{}.so", filename).c_str(),
                          RTLD_NOW | RTLD_GLOBAL);

        if (!handlewp) {
            std::cerr << std::format("Cannot wrapper library: {}\n", dlerror());
            // return false;
        }
    }

    int dlOpenFlags = RTLD_NOW | RTLD_GLOBAL;

    std::string library = path;

    if (filename.ends_with(".a") || filename.ends_with(".o")) {
        // g++  -Wl,--whole-archive base64.cc.o -Wl,--no-whole-archive -shared
        // -o teste.o
        library = std::format("./{}.so", filename);
        std::string cmd = std::format(
            "g++ -Wl,--whole-archive {} -Wl,--no-whole-archive -shared -o {}",
            path, library);
        std::cout << cmd << std::endl;
        system(cmd.c_str());
    }

    auto load_start = std::chrono::steady_clock::now();

    execution::getGlobalExecutionState().setLastLibrary(library);

    resolveSymbolOffsetsFromLibraryFile(functions);

    void *handle =
        dlopen(execution::getGlobalExecutionState().getLastLibrary().c_str(),
               dlOpenFlags);
    if (!handle) {
        std::cerr << __FILE__ << ":" << __LINE__
                  << " Cannot open library: " << dlerror() << '\n';
        return false;
    }

    execution::getGlobalExecutionState().clearSymbolsToResolve();

    auto load_end = std::chrono::steady_clock::now();

    std::cout << "load time: "
              << std::chrono::duration_cast<std::chrono::microseconds>(
                     load_end - load_start)
                     .count()
              << "us" << std::endl;

    fillWrapperPtrs(functions, handlewp, handle);

    return true;
}

auto compileAndRunCode(CompilerCodeCfg &&cfg) -> EvalResult {
    std::vector<VarDecl> vars;
    int returnCode = 0;

    auto now = std::chrono::steady_clock::now();

    if (cfg.sourcesList.empty()) {
        if (cfg.analyze) {
            std::tie(vars, returnCode) = buildLibAndDumpASTWithoutPrint(
                cfg.compiler, cfg.repl_name,
                {std::format("{}.cpp", cfg.repl_name)}, cfg.std);
        } else {
            onlyBuildLib(cfg.compiler, cfg.repl_name, "." + cfg.extension,
                         cfg.std);
        }
    } else {
        std::tie(vars, returnCode) = buildLibAndDumpASTWithoutPrint(
            cfg.compiler, cfg.repl_name, cfg.sourcesList, cfg.std);
    }

    if (returnCode != 0) {
        replState.shouldRecompilePrecompiledHeader = true;
        return {};
    }

    auto end = std::chrono::steady_clock::now();

    {
        auto vars2 = utility::getBuiltFileDecls(
            std::format("./lib{}.so", cfg.repl_name));

        // add only if it does not exist
        for (const auto &var : vars2) {
            if (std::find_if(vars.begin(), vars.end(),
                             [&](const VarDecl &varInst) {
                                 return varInst.mangledName == var.mangledName;
                             }) == vars.end()) {
                if (verbosityLevel >= 2) {
                    std::cout << __FILE__ << ":" << __LINE__
                              << " added: " << var.name << std::endl;
                }
                vars.push_back(var);
            }
        }
    }

    if (verbosityLevel >= 2) {
        std::cout << "⏱️  Build time: "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(end -
                                                                           now)
                         .count()
                  << "ms" << std::endl;
    }

    return prepareWraperAndLoadCodeLib(cfg, std::move(vars));
}

// Função para identificar se o código é uma definição ou código executável
bool isDefinitionCode(const std::string &code) {
    // Remove comentários e espaços em branco para análise
    std::string trimmed = code;

    // Remove comentários de linha
    size_t pos = 0;
    while ((pos = trimmed.find("//", pos)) != std::string::npos) {
        size_t end = trimmed.find('\n', pos);
        if (end == std::string::npos)
            end = trimmed.length();
        trimmed.erase(pos, end - pos);
    }

    // Remove comentários de bloco
    pos = 0;
    while ((pos = trimmed.find("/*", pos)) != std::string::npos) {
        size_t end = trimmed.find("*/", pos + 2);
        if (end == std::string::npos)
            break;
        trimmed.erase(pos, end + 2 - pos);
    }

    // Remove espaços em branco extras
    trimmed = std::regex_replace(trimmed, std::regex(R"(\s+)"), " ");
    trimmed = std::regex_replace(trimmed, std::regex(R"(^\s+|\s+$)"), "");

    if (trimmed.empty())
        return false;

    // Patterns para definições (global scope)
    std::vector<std::regex> definitionPatterns = {
        // Declarações de classe/struct/enum
        std::regex(R"(^\s*(class|struct|enum|union)\s+\w+)"),
        std::regex(R"(^\s*(template\s*<[^>]*>\s*)?(class|struct)\s+\w+)"),

        // Declarações de namespace
        std::regex(R"(^\s*namespace\s+\w+)"),
        std::regex(R"(^\s*using\s+namespace\s+)"),
        std::regex(R"(^\s*using\s+\w+\s*=)"),

        // Declarações de função (incluindo templates)
        std::regex(
            R"(^\s*(template\s*<[^>]*>\s*)?[\w:]+\s+\w+\s*\([^)]*\)\s*(const\s*)?(noexcept\s*)?(\s*->\s*[\w:]+)?\s*[{;])"),

        // Declarações de variáveis globais (com tipos explícitos)
        std::regex(
            R"(^\s*(extern\s+)?(const\s+|constexpr\s+)?(static\s+)?[\w:]+\s+\w+(\s*=.*)?;)"),
        std::regex(
            R"(^\s*(auto|int|float|double|char|bool|string|std::[\w:]+)\s+\w+(\s*=.*)?;)"),

        // Typedef e type aliases
        std::regex(R"(^\s*typedef\s+)"),
        std::regex(R"(^\s*using\s+\w+\s*=\s*)"),

        // Forward declarations
        std::regex(R"(^\s*(class|struct|enum)\s+\w+\s*;)"),

        // Preprocessor directives (exceto #eval, #return, etc.)
        std::regex(
            R"(^\s*#(define|undef|ifdef|ifndef|if|else|elif|endif|include)\s)"),
    };

    // Verifica se corresponde a algum pattern de definição
    for (const auto &pattern : definitionPatterns) {
        if (std::regex_search(trimmed, pattern)) {
            return true;
        }
    }

    // Patterns para código executável (statements)
    std::vector<std::regex> executablePatterns = {
        // Chamadas de função simples
        std::regex(R"(^\s*\w+\s*\([^)]*\)\s*;?\s*$)"),

        // Expressões de atribuição
        std::regex(R"(^\s*\w+\s*[+\-*/%&|^]?=)"),

        // Estruturas de controle
        std::regex(
            R"(^\s*(if|for|while|do|switch|try|catch|throw|return)\s*[\(\{])"),

        // Operadores de incremento/decremento
        std::regex(R"(^\s*(\+\+\w+|\w+\+\+|--\w+|\w+--)\s*;?\s*$)"),

        // Expressões cout/printf
        std::regex(R"(^\s*(std::)?(cout|printf|scanf|cin)\s*[<<>>])"),

        // Statements simples sem declaração de tipo
        std::regex(R"(^\s*\w+(\.\w+|\[\w*\])*\s*[+\-*/%&|^<>=!])")};

    // Se corresponde a pattern executável, não é definição
    for (const auto &pattern : executablePatterns) {
        if (std::regex_search(trimmed, pattern)) {
            return false;
        }
    }

    // Casos especiais: se contém apenas expressões/statements sem declarações
    // e não termina em ';' ou '{}', provavelmente é código executável
    if (!std::regex_search(trimmed, std::regex(R"([;{}]\s*$)")) &&
        std::regex_search(trimmed,
                          std::regex(R"(\w+\s*[+\-*/%<>=!]|\w+\s*\()"))) {
        return false;
    }

    // Se chegou até aqui e não foi identificado como executável,
    // provavelmente é uma definição
    return true;
}

// moved into replState

auto execRepl(std::string_view lineview, int64_t &i) -> bool {
    std::string line(lineview);
    if (line == "exit") {
        return false;
    }

    if (handleReplCommand(line, buildSettings, replState.useCpp2)) {
        return true;
    }

    if (line.starts_with("#include")) {
        std::regex includePattern(R"(#include\s*["<]([^">]+)[">])");
        std::smatch match;

        if (std::regex_search(line, match, includePattern)) {
            if (match.size() > 1) {
                std::cout << "File name: " << match[1] << std::endl;

                std::filesystem::path p(match[1]);

                try {
                    p = std::filesystem::absolute(
                        std::filesystem::canonical(p));
                } catch (...) {
                }

                std::string path = p.string();

                if (p.filename() != "decl_amalgama.hpp" &&
                    p.filename() != "printerOutput.hpp") {
                    // TODO: Implementar recompilação baseada em contexto AST
                    replState.shouldRecompilePrecompiledHeader = true;
                }
            } else {
                std::cout << "File name not found" << std::endl;
                return true;
            }
        } else {
            std::cout << "File name not found" << std::endl;
            return true;
        }
        return true;
    }

    if (replState.shouldRecompilePrecompiledHeader) {
        build_precompiledheader();
        replState.shouldRecompilePrecompiledHeader = false;
    }

    if (line == "printall") {
        std::cout << "printall" << std::endl;

        savePrintAllVarsLibrary(replState.allTheVariables);
        runPrintAll();
        return true;
    }

    if (line == "evalall") {
        evalEverything();
        return true;
    }

    // command parsing handled above

    if (replState.varsNames.contains(line)) {
        auto it = replState.varPrinterAddresses.find(line);

        if (it != replState.varPrinterAddresses.end()) {
            it->second();
            return true;
        } else {
            std::cout << "not found" << std::endl;
        }

        std::cout << "printdata(" << line << ");" << std::endl;

        std::fstream printerOutput("printerOutput.cpp",
                                   std::ios::out | std::ios::trunc);

        printerOutput << "#include \"decl_amalgama.hpp\"\n\n" << std::endl;

        printerOutput << "void printall() {\n";

        printerOutput << "printdata(" << line << ");\n";

        printerOutput << "}\n";

        printerOutput.close();

        onlyBuildLib("clang++", "printerOutput");

        runPrintAll();

        return true;
    }

    CompilerCodeCfg cfg;

    cfg.lazyEval = line.starts_with("#lazyeval ");
    cfg.use_cpp2 = replState.useCpp2;

    if (line.starts_with("#batch_eval ")) {
        line = line.substr(11);

        std::string file;
        std::istringstream iss(line);
        while (std::getline(iss, file, ' ')) {
            cfg.sourcesList.push_back(file);
        }

        cfg.addIncludes = false;
        cfg.fileWrap = false;
    }

    if (line.starts_with("#eval ") || line.starts_with("#lazyeval ")) {
        line = line.substr(line.find_first_of(' ') + 1);
        line = line.substr(0, line.find_last_not_of(" \t\n\v\f\r\0") + 1);

        if (std::filesystem::exists(line)) {
            cfg.fileWrap = false;

            cfg.sourcesList.push_back(line);
            auto textension = line.substr(line.find_last_of('.') + 1);
            /*std::fstream file(line, std::ios::in);


            line.clear();
            std::copy(std::istreambuf_iterator<char>(file),
                      std::istreambuf_iterator<char>(),
                      std::back_inserter(line));*/

            std::cout << "extension: " << textension << std::endl;

            if (textension == "h" || textension == "hpp") {
                cfg.addIncludes = false;
            }

            cfg.use_cpp2 = (textension == "cpp2");

            if (textension == "c") {
                cfg.extension = textension;
                cfg.analyze = false;
                cfg.addIncludes = false;

                cfg.std = "c17";
                cfg.compiler = "clang";
            }
        } else {
            // Detecção inteligente: definição vs código executável
            if (isDefinitionCode(line)) {
                // É uma definição - adiciona ao escopo global sem exec()
                if (verbosityLevel >= 2) {
                    std::cout << "🔧 Detected global definition\n";
                }
                // Não envolver em exec(), deixar como definição global
                cfg.analyze = true; // Queremos analisar definições para AST
            } else {
                // É código executável - envolver em exec()
                if (verbosityLevel >= 2) {
                    std::cout << "⚡ Detected executable code\n";
                }
                line = "void exec() { " + line + "; }\n";
                cfg.analyze = false;
            }
        }
    }

    if (line.starts_with("#return ")) {
        line = line.substr(8);

        line = "void exec() { printdata(((" + line + ")), " +
               utility::quote(line) + ", typeid(decltype((" + line +
               "))).name()); }\n";

        cfg.analyze = false;
    }

    if (auto rerun = replState.evalResults.find(line);
        rerun != replState.evalResults.end()) {
        try {
            if (rerun->second.exec) {
                if (verbosityLevel >= 2) {
                    std::cout << "🔄 Rerunning cached command" << std::endl;
                }
                rerun->second.exec();
                return true;
            }
        } catch (const segvcatch::interrupted_by_the_user &e) {
            std::cerr << "Interrupted by the user: " << e.what() << std::endl;
            return true;
        } catch (const segvcatch::hardware_exception &e) {
            std::cerr << "Hardware exception: " << e.what() << std::endl;
            std::cerr << assembly_info::getInstructionAndSource(
                             getpid(), reinterpret_cast<uintptr_t>(e.info.addr))
                      << std::endl;
            auto [btrace, size] = backtraced_exceptions::get_backtrace_for(e);

            if (btrace != nullptr && size > 0) {
                std::cerr << "Backtrace:\n";
                backtraced_exceptions::print_backtrace(btrace, size);
            } else {
                std::cerr << "Backtrace not available\n";
            }
        } catch (const std::exception &e) {
            std::cerr << std::format("C++ exception on exec/eval: {}\n",
                                     e.what());

            auto [btrace, size] = backtraced_exceptions::get_backtrace_for(e);

            if (btrace != nullptr && size > 0) {
                std::cerr << "Backtrace:\n";
                backtraced_exceptions::print_backtrace(btrace, size);
            } else {
                std::cerr << "Backtrace not available\n";
            }
        } catch (...) {
            std::cerr << "Unknown C++ exception on exec/eval\n";
        }

        return true;
    }

    // std::cout << line << std::endl;

    // Se chegou até aqui e não foi processado por comando especial,
    // aplica detecção inteligente para código normal
    if (cfg.fileWrap && !line.starts_with("#eval") &&
        !line.starts_with("#lazyeval") && !line.starts_with("#return") &&
        !line.starts_with("#batch_eval")) {

        if (isDefinitionCode(line)) {
            // É uma definição - mantém no escopo global
            if (verbosityLevel >= 2) {
                std::cout << "🔧 Global definition detected\n";
            }
            cfg.analyze = true; // Analisar AST para definições
        } else {
            // É código executável - envolver em exec()
            if (verbosityLevel >= 2) {
                std::cout << "⚡ Executable code detected\n";
            }
            line = "void exec() { " + line + "; }\n";
            cfg.analyze = false;
        }
    }

    cfg.repl_name = std::format("repl_{}", i++);

    if (cfg.fileWrap) {
        std::fstream replOutput(
            std::format("{}.{}", cfg.repl_name,
                        (cfg.use_cpp2 ? "cpp2" : cfg.extension)),
            std::ios::out | std::ios::trunc);

        if (cfg.addIncludes) {
            replOutput << "#include \"precompiledheader.hpp\"\n\n";
            replOutput << "#include \"decl_amalgama.hpp\"\n\n";
        }

        replOutput << line << std::endl;

        replOutput.close();
    }

    if (cfg.use_cpp2) {
        int cppfrontres =
            system(("./cppfront " + cfg.repl_name + ".cpp2").c_str());

        if (cppfrontres != 0) {
            std::cerr << "cppfrontres != 0: " << cfg.repl_name << std::endl;
            return false;
        }
    }

    auto evalRes = compileAndRunCode(std::move(cfg));

    if (evalRes.success) {
        replState.evalResults.insert_or_assign(line, evalRes);
    }

    return true;
}

int __attribute__((visibility("default"))) (*bootstrapProgram)(
    int argc, char **argv) = nullptr;

int64_t replCounter = 0;

auto compileAndRunCodeCustom(
    const std::unordered_map<std::string, std::string> &commands,
    const std::vector<std::string> &objects) -> EvalResult {
    auto now = std::chrono::steady_clock::now();

    auto [vars, returnCode] = analyzeCustomCommands(commands);

    if (returnCode != 0) {
        return {};
    }

    CompilerCodeCfg cfg;
    cfg.repl_name = std::format("custom_lib_{}", replCounter++);

    returnCode = linkAllObjects(objects, cfg.repl_name);

    if (returnCode != 0) {
        return {};
    }

    mergeVars(vars);

    auto end = std::chrono::steady_clock::now();

    std::cout << "build time: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(end -
                                                                       now)
                     .count()
              << "ms" << std::endl;

    return prepareWraperAndLoadCodeLib(cfg, std::move(vars));
}

int ctrlcounter = 0;

void handleCtrlC(const segvcatch::hardware_exception_info &info) {
    std::cout << "Ctrl-C pressed" << std::endl;

    if (ctrlcounter == 0) {
        std::cout << "Press Ctrl-C again to exit" << std::endl;
        ctrlcounter++;
    }

    throw segvcatch::interrupted_by_the_user("Ctrl-C pressed", info);
}

void installCtrlCHandler() { segvcatch::init_ctrlc(&handleCtrlC); }

auto extExecRepl(std::string_view lineview) -> bool {
    return execRepl(lineview, replCounter);
}

void repl() {
    std::string line;

    std::string_view historyFile = "history.txt";
    if (read_history(historyFile.data()) != 0) {
        perror("Failed to read history");
    }

    // Mostrar comando de boas-vindas automaticamente apenas se verbosidade >= 1
    if (verbosityLevel >= 1) {
        std::cout
            << "\n🎉 Welcome to C++ REPL - Interactive C++ Development!\n";
        std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
                     "━━━━━━━━━━━━━━━━━━\n";
        std::cout << "💡 Quick tip: Type '#help' for commands, '#welcome' for "
                     "full guide\n\n";
    }

    while (true) {
        // Prompt counter para numeração das linhas
        static int promptCounter = 1;

        try {
            // Prompt mais amigável com indicador de linha
            std::string prompt = std::format("C++[{}]>>> ", promptCounter);
            char *input = readline(prompt.c_str());

            if (input == nullptr) {
                std::cout << "\n👋 Goodbye! Thanks for using C++ REPL.\n";
                break;
            }

            line = input;
            free(input);

            // Skip empty lines
            if (line.empty() ||
                std::all_of(line.begin(), line.end(), ::isspace)) {
                continue;
            }

        } catch (const segvcatch::interrupted_by_the_user &e) {
            if (verbosityLevel >= 1) {
                std::cout << "\n⚠️  Interrupted by user (Ctrl-C)\n";
                std::cout << "💡 Type 'exit' to quit gracefully\n";
            }
            continue;
        }

        ctrlcounter = 0;
        add_history(line.c_str());

        // Show feedback for long operations
        auto start_time = std::chrono::steady_clock::now();

        try {
            if (!extExecRepl(line)) {
                break;
            }

            promptCounter++;

            // Show timing for operations longer than 100ms (apenas com
            // verbosidade >= 2)
            auto end_time = std::chrono::steady_clock::now();
            auto duration =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    end_time - start_time);
            if (verbosityLevel >= 2 && duration.count() > 100) {
                std::cout << std::format("⏱️  Execution time: {}ms\n",
                                         duration.count());
            }

        } catch (const segvcatch::segmentation_fault &e) {
            std::cerr << std::format("💥 Segmentation fault: {}\n", e.what());
            if (verbosityLevel >= 3) {
                std::cerr << "🔍 Debug info: ";
                std::cerr << assembly_info::getInstructionAndSource(
                                 getpid(),
                                 reinterpret_cast<uintptr_t>(e.info.addr))
                          << std::endl;
            }
            if (verbosityLevel >= 1) {
                std::cerr
                    << "💡 Tip: Use '-s' flag to enable crash protection\n";
            }
        } catch (const segvcatch::floating_point_error &e) {
            std::cerr << std::format("🔢 Floating point error: {}\n", e.what());
            if (verbosityLevel >= 3) {
                std::cerr << "🔍 Debug info: ";
                std::cerr << assembly_info::getInstructionAndSource(
                                 getpid(),
                                 reinterpret_cast<uintptr_t>(e.info.addr))
                          << std::endl;
            }
        } catch (const std::exception &e) {
            std::cerr << std::format("❌ C++ Exception: {}\n", e.what());
            if (verbosityLevel >= 1) {
                std::cerr << "💡 Check your C++ syntax and try again\n";
            }
        }

        if (bootstrapProgram) {
            break;
        }
    }

    if (write_history(historyFile.data()) != 0) {
        perror("Failed to write history");
    }
}

int ext_build_precompiledheader() { return build_precompiledheader(); }

void initRepl() {
    writeHeaderPrintOverloads();
    build_precompiledheader();

    // Criar um contexto AST inicial para inicializar o arquivo
    // decl_amalgama.hpp
    auto context = std::make_shared<analysis::AstContext>();
    context->addDeclaration("#pragma once");
    context->addDeclaration("#include \"precompiledheader.hpp\"");

    context->saveHeaderToFile("decl_amalgama.hpp");
}

void initNotifications(std::string_view appName) {
#ifndef NUSELIBNOTIFY
    notify_init(appName.data());
#else
    std::cerr << "Notifications not available" << std::endl;
#endif
}

void notifyError(std::string_view summary, std::string_view msg) {
#ifndef NUSELIBNOTIFY
    NotifyNotification *notification = notify_notification_new(
        summary.data(), msg.data(), "/home/fabio/Downloads/icons8-erro-64.png");

    // Set the urgency level of the notification
    notify_notification_set_urgency(notification, NOTIFY_URGENCY_CRITICAL);

    // Show the notification
    notify_notification_show(notification, NULL);
#else
    std::cerr << summary << ": " << msg << std::endl;
#endif
}

std::any getResultRepl(std::string cmd) {
    lastReplResult = std::any();

    cmd.insert(0, "#eval lastReplResult = (");
    cmd += ");";

    execRepl(cmd, replCounter);

    return lastReplResult;
}
