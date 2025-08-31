#include "stdafx.hpp"

#include "backtraced_exceptions.hpp"
#include "printerOverloads.hpp"

#include "analysis/ast_analyzer.hpp"
#include "analysis/ast_context.hpp"
#include "analysis/clang_ast_adapter.hpp"
#include "compiler/compiler_service.hpp"
#include "completion/simple_readline_completion.hpp"
#include "execution/execution_engine.hpp"
#include "execution/symbol_resolver.hpp"
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
#include <libnotify/notify.h>
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

// Completion scope para autocompletion
static std::unique_ptr<completion::SimpleCompletionScope> completionScope;

static std::string_view trim(const std::string_view str) noexcept {
    auto is_not_blank = [](unsigned char ch) {
        return !std::isblank(ch) && ch != '\n' && ch != '\r';
    };

    auto first = std::find_if(str.begin(), str.end(), is_not_blank);
    if (first == str.end()) {
        return {};
    }
    auto last = std::find_if(str.rbegin(), str.rend(), is_not_blank).base();
    return str.substr(std::distance(str.begin(), first),
                      std::distance(first, last));
}

// Callback para merge de variáveis no CompilerService
static void mergeVarsCallback(const std::vector<VarDecl> &vars) {
    for (const auto &var : vars) {
        if (replState.varsNames.find(var.name) == replState.varsNames.end()) {
            replState.varsNames.insert(var.name);
            replState.allTheVariables.push_back(var);
        }
    }

    // Atualizar completion com novo contexto
    if (completionScope) {
        completionScope->updateContext(replState);
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

    // Atualizar completion
    if (completionScope) {
        completionScope->updateContext(replState);
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

auto buildLibAndDumpASTWithoutPrint(std::string compiler,
                                    const std::string &libname,
                                    const std::vector<std::string> &names,
                                    const std::string &std)
    -> std::pair<std::vector<VarDecl>, int> {
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

void prepareFunctionWrapper(
    const std::string &name, const std::vector<VarDecl> &vars,
    std::unordered_map<std::string, std::string> &functions) {

    // Obter funções existentes do execution state
    std::unordered_set<std::string> existingFunctions;
    auto &state = execution::getGlobalExecutionState();
    // TODO: Implementar método para obter funções existentes do state

    // Usar a configuração global persistente
    auto &config = state.getWrapperConfig();

    functions = execution::SymbolResolver::prepareFunctionWrapper(
        name, vars, config, existingFunctions);
}

void fillWrapperPtrs(
    const std::unordered_map<std::string, std::string> &functions,
    void *handlewp, void *handle) {

    // Usar a configuração global persistente
    auto &config = execution::getGlobalExecutionState().getWrapperConfig();

    execution::SymbolResolver::fillWrapperPtrs(functions, handlewp, handle,
                                               config);
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

    auto libraryPath = execution::getGlobalExecutionState().getLastLibrary();
    auto symbolOffsets =
        execution::SymbolResolver::resolveSymbolOffsetsFromLibraryFile(
            functions, libraryPath);

    // Adicionar offsets ao execution state
    for (const auto &[symbol, offset] : symbolOffsets) {
        execution::getGlobalExecutionState().addSymbolToResolve(symbol, offset);
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

    // Inicializar configuração global para trampolines
    execution::getGlobalExecutionState().initializeWrapperConfig();

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
    lineview = trim(lineview);
    std::string line(lineview);
    if (line == "exit") {
        return false;
    }

    if (handleReplCommand(line, buildSettings, replState.useCpp2)) {
        return true;
    }

    /**
     * If this is multiline, this must be a code block, not just an include
     * directive
     */
    if (line.starts_with("#include") &&
        line.find_first_of('\n') == std::string::npos) {
        std::regex includePattern(R"(#include\s*["<]([^">]+)[">])");
        std::smatch match;

        if (std::regex_search(line, match, includePattern)) {
            if (match.size() > 1) {
                std::string fileName = match[1].str();
                if (verbosityLevel >= 1) {
                    std::cout
                        << std::format("📁 Including file: {}\n", fileName);
                }

                std::filesystem::path p(fileName);

                try {
                    p = std::filesystem::absolute(
                        std::filesystem::canonical(p));

                    if (verbosityLevel >= 2) {
                        std::cout << std::format("   → Resolved path: {}\n",
                                                 p.string());
                    }
                } catch (const std::filesystem::filesystem_error &e) {
                    if (verbosityLevel >= 2) {
                        std::cout << std::format(
                            "   ⚠️  Warning: Could not canonicalize path - {}\n",
                            e.what());
                    }

                    if (!compilerService->checkIncludeExists(buildSettings,
                                                             p.string())) {
                        std::cerr << std::format(
                            "❌ Error: Included file does not exist: {}\n",
                            p.string());
                        return true;
                    }
                }

                if (p.filename() != "decl_amalgama.hpp" &&
                    p.filename() != "printerOutput.hpp") {
                    // TODO: Implementar recompilação baseada em contexto AST
                    replState.shouldRecompilePrecompiledHeader =
                        analysis::AstContext::addInclude(p.string());

                    if (replState.shouldRecompilePrecompiledHeader) {
                        analysis::AstContext::staticSaveHeaderToFile(
                            "decl_amalgama.hpp");
                    }

                    if (replState.shouldRecompilePrecompiledHeader &&
                        verbosityLevel >= 2) {
                        std::cout
                            << "   🔄 Marked for precompiled header rebuild\n";
                    }
                }
            } else {
                std::cerr << "❌ Error: Could not parse include directive\n";
                return true;
            }
        } else {
            std::cerr << "❌ Error: Invalid include syntax. Use: #include "
                         "<header> or #include \"header\"\n";
            return true;
        }
        return true;
    }

    if (replState.shouldRecompilePrecompiledHeader) {
        std::cout << "🔨 Rebuilding precompiled header...\n";
        build_precompiledheader();
        replState.shouldRecompilePrecompiledHeader = false;
        std::cout << "✅ Precompiled header rebuilt successfully\n";
    }

    if (line == "printall") {
        std::cout << "📊 Printing all variables...\n";
        savePrintAllVarsLibrary(replState.allTheVariables);
        runPrintAll();
        return true;
    }

    if (line == "evalall") {
        std::cout << "⚡ Evaluating all lazy expressions...\n";
        evalEverything();
        return true;
    }

    // command parsing handled above

    if (replState.varsNames.contains(line)) {
        auto it = replState.varPrinterAddresses.find(line);

        if (it != replState.varPrinterAddresses.end()) {
            std::cout << std::format("🔍 Printing variable: {}\n", line);
            it->second();
            return true;
        } else {
            std::cerr << std::format("❌ Error: Variable '{}' found in names "
                                     "but not in printer addresses\n",
                                     line);
        }

        std::cout << std::format("🔧 Generating printer for variable: {}\n",
                                 line);

        std::fstream printerOutput("printerOutput.cpp",
                                   std::ios::out | std::ios::trunc);

        printerOutput << "#include \"decl_amalgama.hpp\"\n\n" << std::endl;

        printerOutput << "void printall() {\n";
        printerOutput << std::format("    printdata({});\n", line);
        printerOutput << "}\n";

        printerOutput.close();

        std::cout << "📦 Building printer library...\n";
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

            std::cout << std::format("📄 Processing file: {} (extension: {})\n",
                                     line, textension);

            if (textension == "h" || textension == "hpp") {
                cfg.addIncludes = false;
                std::cout << "   → Header file detected - includes disabled\n";
            }

            if (textension == "cpp2") {
                cfg.use_cpp2 = true;
                std::cout << "   → cpp2 mode enabled\n";
            }

            if (textension == "c") {
                cfg.extension = textension;
                cfg.analyze = false;
                cfg.addIncludes = false;
                cfg.std = "c17";
                cfg.compiler = "clang";
                std::cout << "   → C source detected - using clang with C17 "
                             "standard\n";
            }
        } else {
            // Detecção inteligente: definição vs código executável
            if (isDefinitionCode(line)) {
                // É uma definição - adiciona ao escopo global sem exec()
                if (verbosityLevel >= 2) {
                    std::cout << "🔧 Detected global definition\n";
                } else if (verbosityLevel >= 1) {
                    std::cout << std::format(
                        "🔧 Global definition: {}\n",
                        line.length() > 50 ? line.substr(0, 47) + "..." : line);
                }
                // Não envolver em exec(), deixar como definição global
                cfg.analyze = true; // Queremos analisar definições para AST
            } else {
                // É código executável - envolver em exec()
                if (verbosityLevel >= 2) {
                    std::cout << "⚡ Detected executable code\n";
                } else if (verbosityLevel >= 1) {
                    std::cout << std::format(
                        "⚡ Executable: {}\n",
                        line.length() > 50 ? line.substr(0, 47) + "..." : line);
                }
                line = std::format("void exec() {{ {}; }}\n", line);
                cfg.analyze = false;
            }
        }
    }

    if (line.starts_with("#return ")) {
        std::string expression = line.substr(8);

        if (verbosityLevel >= 1) {
            std::cout << std::format("🔍 Evaluating expression: {}\n",
                                     expression);
        }

        line = std::format("void exec() {{ printdata((({0})), {1}, "
                           "typeid(decltype(({0}))).name()); }}",
                           expression, utility::quote(expression));

        cfg.analyze = false;
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
            } else if (verbosityLevel >= 1) {
                std::cout << std::format(
                    "🔧 Definition: {}\n",
                    line.length() > 40 ? line.substr(0, 37) + "..." : line);
            }
            cfg.analyze = true; // Analisar AST para definições
        } else {
            // É código executável - envolver em exec()
            if (verbosityLevel >= 2) {
                std::cout << "⚡ Executable code detected\n";
            } else if (verbosityLevel >= 1) {
                std::cout << std::format(
                    "⚡ Code: {}\n",
                    line.length() > 40 ? line.substr(0, 37) + "..." : line);
            }
            line = std::format("void exec() {{ {}; }}\n", line);
            cfg.analyze = false;
        }
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

    cfg.repl_name = std::format("repl_{}", i++);

    if (cfg.fileWrap) {
        std::string fileName = std::format(
            "{}.{}", cfg.repl_name, (cfg.use_cpp2 ? "cpp2" : cfg.extension));

        if (verbosityLevel >= 2) {
            std::cout << std::format("📝 Writing source to: {}\n", fileName);
        }

        std::fstream replOutput(fileName, std::ios::out | std::ios::trunc);

        if (cfg.addIncludes) {
            replOutput << "#include \"precompiledheader.hpp\"\n\n";
            replOutput << "#include \"decl_amalgama.hpp\"\n\n";
        }

        replOutput << line << std::endl;
        replOutput.close();
    }

    if (cfg.use_cpp2) {
        std::string cppfrontCmd =
            std::format("./cppfront {}.cpp2", cfg.repl_name);

        if (verbosityLevel >= 1) {
            std::cout << std::format("🔄 Running cppfront: {}\n", cppfrontCmd);
        }

        int cppfrontres = system(cppfrontCmd.c_str());

        if (cppfrontres != 0) {
            std::cerr << std::format(
                "❌ cppfront failed with code {} for: {}\n", cppfrontres,
                cfg.repl_name);
            return false;
        }
    }

    if (verbosityLevel >= 1) {
        std::cout << std::format("🚀 Compiling and executing: {}\n",
                                 cfg.repl_name);
    }

    auto evalRes = compileAndRunCode(std::move(cfg));

    if (evalRes.success) {
        replState.evalResults.insert_or_assign(line, evalRes);
        if (verbosityLevel >= 2) {
            std::cout << "✅ Command executed successfully and cached\n";
        }
    } else if (verbosityLevel >= 1) {
        std::cout << "❌ Command execution failed\n";
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

    // Inicializar completion com estado atual do REPL
    completionScope =
        std::make_unique<completion::SimpleCompletionScope>(replState);

    // Criar um contexto AST inicial para inicializar o arquivo
    // decl_amalgama.hpp
    auto context = std::make_shared<analysis::AstContext>();
    context->addDeclaration("#pragma once");
    context->addDeclaration("#include \"precompiledheader.hpp\"");

    context->saveHeaderToFile("decl_amalgama.hpp");
}

void shutdownRepl() {
    if (completionScope) {
        completionScope.reset();
    }
#ifndef NUSELIBNOTIFY
    notify_uninit();
#endif
}

void initNotifications(std::string_view appName) {
#ifndef NUSELIBNOTIFY
    if (notify_is_initted()) {
        return;
    }

    notify_init(appName.data());
#else
    std::cerr << "Notifications not available" << std::endl;
#endif
}

void notifyError(std::string_view summary, std::string_view msg) {
#ifndef NUSELIBNOTIFY
    // Função para encontrar ícone com fallback
    auto findIcon = [](const std::string &iconName) -> std::string {
        std::vector<std::string> searchPaths;

        // 1. Tentar PNG path primeiro (se disponível)
#ifdef CPPREPL_ICON_PNG_PATH
        searchPaths.push_back(std::string(CPPREPL_ICON_PNG_PATH) + "/" +
                              iconName + ".png");
#endif

        // 2. Tentar SVG path
#ifdef CPPREPL_ICON_PATH
        searchPaths.push_back(std::string(CPPREPL_ICON_PATH) + "/" + iconName +
                              ".svg");
#endif

        // 3. Fallback para ícones do sistema
        searchPaths.push_back("/usr/share/icons/hicolor/48x48/apps/error.png");
        searchPaths.push_back("/usr/share/pixmaps/dialog-error.png");
        searchPaths.push_back("dialog-error"); // Nome simbólico

        for (const auto &path : searchPaths) {
            if (path.find('/') != std::string::npos) {
                // É um caminho de arquivo - verificar se existe
                if (std::filesystem::exists(path)) {
                    return path;
                }
            } else {
                // É um nome simbólico - retornar diretamente
                return path;
            }
        }

        return "dialog-error"; // Fallback final
    };

    std::string iconPath = findIcon("circle-alert");

    NotifyNotification *notification =
        notify_notification_new(summary.data(), msg.data(), iconPath.c_str());

    // Set the urgency level of the notification
    notify_notification_set_urgency(notification, NOTIFY_URGENCY_CRITICAL);

    // Show the notification
    notify_notification_show(notification, NULL);
#else
    std::cerr << std::format("🚨 {}: {}", summary, msg) << std::endl;
#endif
}

std::any getResultRepl(std::string cmd) {
    lastReplResult = std::any();

    cmd.insert(0, "#eval lastReplResult = (");
    cmd += ");";

    execRepl(cmd, replCounter);

    return lastReplResult;
}
