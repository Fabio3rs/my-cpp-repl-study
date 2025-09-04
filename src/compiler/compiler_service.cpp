#include "compiler/compiler_service.hpp"

#include "../../repl.hpp"
#include "analysis/ast_context.hpp"
#include "analysis/clang_ast_adapter.hpp"

#include "utility/system_exec.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <execution>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <future>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

// Forward declaration for helper function from repl.cpp
extern int verbosityLevel;

namespace compiler {

CompilerService::CompilerService(
    const BuildSettings *buildSettings,
    std::shared_ptr<analysis::AstContext> astContext,
    VarMergeCallback varMergeCallback)
    : buildSettings_(buildSettings), astContext_(astContext),
      varMergeCallback_(varMergeCallback) {
    if (!buildSettings_) {
        throw std::invalid_argument("BuildSettings cannot be null");
    }
}

bool CompilerService::checkIncludeExists(const BuildSettings &settings,
                                         const std::string &includePath) {
    auto includeDirsArgs = settings.getIncludeDirectoriesStr();
    std::string command = std::format("clang++ -x c++ -E -P {} - < /dev/null "
                                      "-include {} 2>/dev/null",
                                      includeDirsArgs, includePath);
    auto [output, returnCode] = utility::runProgramGetOutput(command);
    return returnCode == 0;
}

// === Helper Methods ===

std::string
CompilerService::getPrecompiledHeaderFlag(const std::string &ext) const {
    return (ext == ".c") ? "" : " -include precompiledheader.hpp";
}

std::string CompilerService::getLinkLibrariesStr() const {
    return buildSettings_->getLinkLibrariesStr();
}

std::string CompilerService::getIncludeDirectoriesStr() const {
    return buildSettings_->getIncludeDirectoriesStr();
}

std::string CompilerService::getPreprocessorDefinitionsStr() const {
    return buildSettings_->getPreprocessorDefinitionsStr();
}

std::string CompilerService::buildCompileCommand(
    const std::string &compiler, const std::string &std,
    const std::string &flags, const std::string &inputFile,
    const std::string &outputFile) const {
    return std::format("{} -std={} {} {} {} {} {} -o {}", compiler, std, flags,
                       getIncludeDirectoriesStr(),
                       getPreprocessorDefinitionsStr(), inputFile,
                       getLinkLibrariesStr(), outputFile);
}

CompilerResult<int>
CompilerService::executeCommand(const std::string &command) const {
    if (verbosityLevel >= 2) {
        std::cout << "Executing: " << command << std::endl;
    }

    int result = system(command.c_str());

    CompilerResult<int> compilerResult;
    compilerResult.value = result;

    if (result != 0) {
        compilerResult.error = CompilerError::SystemCommandFailed;
        std::cerr << std::format("Command failed with code: {}", result)
                  << std::endl;
    }

    return compilerResult;
}

std::string
CompilerService::concatenateNames(const std::vector<std::string> &names) const {
    std::string concat;
    size_t totalSize = 0;
    for (const auto &name : names) {
        totalSize += name.size() + 1;
    }

    concat.reserve(totalSize);

    for (const auto &name : names) {
        concat += std::format("{} ", name);
    }

    return concat;
}

std::string CompilerService::readLogFile(const std::string &logPath) const {
    if (!std::filesystem::exists(logPath)) {
        return "";
    }

    std::ifstream logFile(logPath);
    if (!logFile.is_open()) {
        return "";
    }

    std::string content;
    std::string line;

    while (std::getline(logFile, line)) {
        content += std::format("{}\n", line);
    }

    return content;
}

void CompilerService::printCompilationError(const std::string &logPath,
                                            const std::string &context) const {
    std::string errorLog = readLogFile(logPath);

    if (!errorLog.empty()) {
        // Header colorido
        std::cerr << std::format("{}=== Compilation Error in {} ==={}",
                                 getColorCode("red"), context,
                                 getColorCode("reset"))
                  << std::endl;

        // Processar cada linha do log com cores
        std::istringstream stream(errorLog);
        std::string line;
        while (std::getline(stream, line)) {
            std::cerr << formatErrorLine(line) << std::endl;
        }

        std::cerr << std::format("{}==============================={}",
                                 getColorCode("red"), getColorCode("reset"))
                  << std::endl;
    }
}

bool CompilerService::shouldUseColors() const {
    // Verifica se o terminal suporta cores
    const char *term = getenv("TERM");
    const char *colorterm = getenv("COLORTERM");
    const char *force_color = getenv("FORCE_COLOR");

    // Se FORCE_COLOR está definido, usar cores
    if (force_color && strlen(force_color) > 0) {
        return true;
    }

    // Verifica se o terminal suporta cores
    return (term && strstr(term, "color")) || (term && strstr(term, "xterm")) ||
           (colorterm && strlen(colorterm) > 0) ||
           isatty(STDERR_FILENO); // Se é um terminal interativo
}

std::string CompilerService::getColorCode(const std::string &color) const {
    if (!shouldUseColors())
        return "";

    if (color == "red")
        return "\033[31m";
    if (color == "yellow")
        return "\033[33m";
    if (color == "green")
        return "\033[32m";
    if (color == "blue")
        return "\033[34m";
    if (color == "magenta")
        return "\033[35m";
    if (color == "cyan")
        return "\033[36m";
    if (color == "bold")
        return "\033[1m";
    if (color == "reset")
        return "\033[0m";
    return "";
}

std::string CompilerService::formatErrorLine(const std::string &line) const {
    std::string formatted = line;

    // Colorir diferentes tipos de mensagem
    if (line.find("error:") != std::string::npos) {
        formatted =
            std::format("{}{}{}{}", getColorCode("red"), getColorCode("bold"),
                        line, getColorCode("reset"));
    } else if (line.find("warning:") != std::string::npos) {
        formatted =
            std::format("{}{}{}{}", getColorCode("yellow"),
                        getColorCode("bold"), line, getColorCode("reset"));
    } else if (line.find("note:") != std::string::npos) {
        formatted = std::format("{}{}{}", getColorCode("blue"), line,
                                getColorCode("reset"));
    } else if (line.find("^") != std::string::npos &&
               line.find("~") != std::string::npos) {
        // Linha de indicação de erro (setas e tildes)
        formatted = std::format("{}{}{}", getColorCode("green"), line,
                                getColorCode("reset"));
    }

    return formatted;
}

// === Core Operations ===

CompilerResult<int> CompilerService::buildLibraryOnly(
    const std::string &compiler, const std::string &name,
    const std::string &ext, const std::string &std, std::string_view extra_args,
    std::string_view pchFile) const {
    std::string includePrecompiledHeader =
        pchFile.empty() ? getPrecompiledHeaderFlag(ext) : std::string(pchFile);

    auto cmd =
        std::format("{} -std={} -shared {} {} {} -g -Wl,--export-dynamic -fPIC "
                    "{}{} {} {} -o lib{}.so",
                    compiler, std, includePrecompiledHeader,
                    getIncludeDirectoriesStr(), getPreprocessorDefinitionsStr(),
                    name, ext, getLinkLibrariesStr(), extra_args, name);

    return executeCommand(cmd);
}

CompilerResult<std::vector<VarDecl>> CompilerService::buildLibraryWithAST(
    const std::string &compiler, const std::string &name,
    const std::string &ext, const std::string &std) const {
    CompilerResult<std::vector<VarDecl>> result;

    std::string includePrecompiledHeader = getPrecompiledHeaderFlag(ext);

    // First build - compile to shared library
    auto cmd =
        std::format("{} -std={} -shared {} {} {} -g -Wl,--export-dynamic -fPIC "
                    "{}{} {} -o lib{}.so",
                    compiler, std, includePrecompiledHeader,
                    getIncludeDirectoriesStr(), getPreprocessorDefinitionsStr(),
                    name, ext, getLinkLibrariesStr(), name);

    auto buildResult = executeCommand(cmd);
    if (!buildResult) {
        result.error = CompilerError::BuildFailed;
        return result;
    }

    // Second build - AST dump
    cmd = std::format(
        "{} -std={} -fcolor-diagnostics -fPIC -Xclang -ast-dump=json {} {} {} "
        "-fsyntax-only {}{} -o lib{}.so > {}.json",
        compiler, std, includePrecompiledHeader, getIncludeDirectoriesStr(),
        getPreprocessorDefinitionsStr(), name, ext, name, name);

    auto astResult = executeCommand(cmd);
    if (!astResult) {
        result.error = CompilerError::AstAnalysisFailed;
        return result;
    }

    // Analyze AST
    analysis::ClangAstAnalyzerAdapter analyzer;
    std::vector<VarDecl> vars;
    int ares = analyzer.analyzeFile(std::format("{}.json", name),
                                    std::format("{}.cpp", name), vars);
    if (ares != 0) {
        result.error = CompilerError::AstAnalysisFailed;
        return result;
    }

    // Merge variables using callback if provided
    if (varMergeCallback_) {
        varMergeCallback_(vars);
    }

    // Third build - final library with proper settings
    cmd = std::format(
        "{}{} {} -std=gnu++20 -shared -include precompiledheader.hpp -g "
        "-Wl,--export-dynamic -fPIC {}.cpp {} -o lib{}.so",
        compiler, getPreprocessorDefinitionsStr(), getIncludeDirectoriesStr(),
        name, getLinkLibrariesStr(), name);

    auto finalBuildResult = executeCommand(cmd);
    if (!finalBuildResult) {
        result.error = CompilerError::BuildFailed;
        return result;
    }

    auto context = analyzer.getContext();
    // Salva o header se houve mudanças
    if (context->hasHeaderChanged()) {
        context->saveHeaderToFile("decl_amalgama.hpp");
    }

    result.value = std::move(vars);
    return result;
}

CompilerResult<void> CompilerService::buildPrecompiledHeader(
    const std::string &compiler,
    std::shared_ptr<analysis::AstContext> context) const {
    CompilerResult<void> result;

    // Use provided context or fall back to member
    auto contextToUse = context ? context : astContext_;

    try {
        std::fstream precompHeader("precompiledheader.hpp",
                                   std::ios::out | std::ios::trunc);

        precompHeader << "#pragma once\n\n" << std::endl;
        precompHeader << "#include <any>\n" << std::endl;
        precompHeader
            << "extern int (*bootstrapProgram)(int argc, char **argv);\n";
        precompHeader << "extern std::any lastReplResult;\n";

        // Se temos um contexto, usar os includes dele
        /*if (contextToUse) {
            precompHeader << contextToUse->getOutputHeader() << std::endl;
        }*/

        for (const auto &inc : analysis::AstContext::getIncludedFiles()) {
            if (inc.second) {
                precompHeader << std::format("#include <{}>\n", inc.first);
            } else {
                precompHeader << std::format("#include \"{}\"\n", inc.first);
            }
        }

        precompHeader.flush();
        precompHeader.close();

    } catch (const std::exception &e) {
        std::cerr << std::format("Failed to write precompiled header: {}\n",
                                 e.what());
        result.error = CompilerError::FileWriteFailed;
        return result;
    }

    std::string cmd = std::format(
        "{}{} {} -fPIC -x c++-header -std=gnu++20 -o precompiledheader.hpp.pch "
        "precompiledheader.hpp",
        compiler, getPreprocessorDefinitionsStr(), getIncludeDirectoriesStr());

    auto cmdResult = executeCommand(cmd);
    if (!cmdResult) {
        result.error = CompilerError::PrecompiledHeaderFailed;
        return result;
    }

    return result;
}

CompilerResult<void> CompilerService::buildCustomPCH(
    const std::string &compiler, const std::string &header,
    const std::string &outputPchFile,
    std::shared_ptr<analysis::AstContext> context) const {
    CompilerResult<void> result;

    if (!std::filesystem::exists(header)) {
        std::cerr << std::format("Header file '{}' does not exist.\n", header);
        result.error = CompilerError::PrecompiledHeaderFailed;
        return result;
    }

    std::string cmd =
        std::format("{}{} {} -fPIC -x c++-header -std=gnu++20 -o {} {}",
                    compiler, getPreprocessorDefinitionsStr(),
                    getIncludeDirectoriesStr(), outputPchFile, header);

    auto cmdResult = executeCommand(cmd);
    if (!cmdResult) {
        result.error = CompilerError::PrecompiledHeaderFailed;
        return result;
    }

    result.error = CompilerError::Success;

    return result;
}

CompilerResult<int>
CompilerService::linkObjects(const std::vector<std::string> &objects,
                             const std::string &libname) const {
    std::string linkLibraries = getLinkLibrariesStr();
    std::string namesConcated = concatenateNames(objects);

    std::string cmd =
        std::format("clang++ -shared -g -WL,--export-dynamic {} {} -o lib{}.so",
                    namesConcated, linkLibraries, libname);

    return executeCommand(cmd);
}

CompilerResult<CompilationResult> CompilerService::buildMultipleSourcesWithAST(
    const std::string &compiler, const std::string &libname,
    const std::vector<std::string> &sources, const std::string &std) const {
    CompilerResult<CompilationResult> result;

    // Early return
    if (sources.empty()) {
        result.value.returnCode = 0;
        return result;
    }

    const std::string includes = getIncludeDirectoriesStr();
    const std::string ppdefs = getPreprocessorDefinitionsStr();
    const auto t0 = std::chrono::system_clock::now();

    std::vector<VarDecl> allVars;
    std::string namesConcated;
    bool hasChanged = false;
    int errorCode = 0;

    struct SourceProcessResult {
        std::string sourceFile;
        std::string purefilename;
        std::string objectName;
        std::vector<VarDecl> localVars;
        bool hasHeaderChanged = false;
        int errorCode = 0;
        std::string errorMessage;
    };

    // Helpers ---------------------------------------------------------------

    auto pureName = [](const std::string &pathStr) {
        const auto path = std::filesystem::path(pathStr);
        auto base = path.filename().string();
        const auto pos = base.find_last_of('.');
        return pos == std::string::npos ? base : base.substr(0, pos);
    };

    auto astCmdFor = [&](const std::string &name, const std::string &pure) {
        // dump JSON AST + usa PCH (mesmo que seu original)
        // saída JSON vai para "<pure>.json" e logs para "<pure>.log"
        return std::format(
            "{}{} {} -std={} -fcolor-diagnostics -fPIC "
            "-Xclang -ast-dump=json "
            "-Xclang -include-pch -Xclang precompiledheader.hpp.pch "
            "-include precompiledheader.hpp -fsyntax-only {} "
            "-o lib{}_blank.so > {}.json 2> {}.log",
            compiler, ppdefs, includes, std, name, pure, pure, pure);
    };

    auto compileCmdFor = [&](const std::string &name, const std::string &obj) {
        return std::format("{}{} {} -std=gnu++20 -fPIC -c -Xclang "
                           "-include-pch -Xclang precompiledheader.hpp.pch "
                           "-include precompiledheader.hpp "
                           "-g -fPIC {} -o {}",
                           compiler, ppdefs, includes, name, obj);
    };

    auto compileAndLinkCmdFor = [&](const std::string &name,
                                    const std::string &obj) {
        auto linkerFlags = buildSettings_->getExtraLinkerFlags();
        return std::format(
            "{}{} {} -std=gnu++20 -shared -include "
            "precompiledheader.hpp -g -Wl,--export-dynamic {} -fPIC {} -o {}",
            compiler, ppdefs, includes, linkerFlags, name, obj);
    };

    auto analyzeAst =
        [&](const std::string &jsonFile, const std::string &srcName,
            std::vector<VarDecl> &outVars, bool &outHeaderChanged) -> int {
        analysis::ClangAstAnalyzerAdapter analyzer;
        int ares = analyzer.analyzeFile(jsonFile, srcName, outVars);
        if (ares == 0) {
            outHeaderChanged = analyzer.getContext()->hasHeaderChanged();
        }
        return ares;
    };

    auto processOne = [&](const std::string &name,
                          bool buildAndLink = false) -> SourceProcessResult {
        SourceProcessResult r;
        r.sourceFile = name;
        if (name.empty()) {
            return r;
        }

        r.purefilename = pureName(name);

        if (buildAndLink) {
            r.objectName = std::format("lib{}.so", r.purefilename);
        } else {
            r.objectName = std::format("{}.o", r.purefilename);
        }

        const std::string astCmd = astCmdFor(name, r.purefilename);
        const std::string ccCmd = buildAndLink
                                      ? compileAndLinkCmdFor(name, r.objectName)
                                      : compileCmdFor(name, r.objectName);

        try {
            // AST + compile em paralelo
            auto futAst = std::async(std::launch::async, [&, astCmd] {
                return utility::runProgramGetOutput(astCmd);
            });
            auto futCC = std::async(std::launch::async, [&, ccCmd] {
                return utility::runProgramGetOutput(ccCmd);
            });

            const auto astRes = futAst.get();
            const auto ccRes = futCC.get();

            if (astRes.second != 0) {
                r.errorCode = astRes.second;
                r.errorMessage =
                    std::format("AST dump failed for {}: {}", name, astCmd);
                return r;
            }
            if (ccRes.second != 0) {
                r.errorCode = ccRes.second;
                r.errorMessage = std::format(
                    "Object compilation failed for {}: {}", name, ccCmd);
                return r;
            }

            // Análise do JSON
            bool headerChanged = false;
            const int ares = analyzeAst(r.purefilename + ".json", name,
                                        r.localVars, headerChanged);
            if (ares != 0) {
                r.errorCode = ares;
                r.errorMessage = std::format(
                    "AST analysis failed for {} with code: {}", name, ares);
                return r;
            }

            r.hasHeaderChanged = headerChanged;
            r.errorCode = 0;
            return r;

        } catch (const std::exception &e) {
            r.errorCode = -1;
            r.errorMessage = std::format("Exception during processing {}: {}",
                                         name, e.what());
            return r;
        }
    };

    auto handleErrorAndBail = [&](const SourceProcessResult &r) {
        if (r.errorCode != 0) {
            std::cerr << r.errorMessage << std::endl;
            if (r.errorCode > 0) {
                printCompilationError(std::format("{}.log", r.purefilename),
                                      "processing for " + r.sourceFile);
            }
            errorCode = r.errorCode;
        }
    };

    // Execução --------------------------------------------------------------
    bool linked = false;

    if (sources.size() == 1) {
        linked = true;
        auto r = processOne(sources.front(), linked);
        if (r.errorCode != 0) {
            handleErrorAndBail(r);
        } else {
            namesConcated += std::format("{} ", r.objectName);
            allVars.insert(allVars.end(), r.localVars.begin(),
                           r.localVars.end());
            hasChanged |= r.hasHeaderChanged;
        }
    } else {
        const size_t maxConc =
            std::min(getEffectiveThreadCount(), sources.size());
        std::vector<std::future<SourceProcessResult>> futures;
        futures.reserve(sources.size());

        // Lança com capping simples (como seu original)
        for (size_t i = 0; i < sources.size(); ++i) {
            const auto &name = sources[i];
            if (name.empty())
                continue;
            auto policy =
                (i < maxConc) ? std::launch::async : std::launch::deferred;
            futures.emplace_back(std::async(policy, processOne, name));
        }

        for (auto &f : futures) {
            auto r = f.get();
            if (r.errorCode != 0) {
                handleErrorAndBail(r);
                break; // comportamento compatível: para no primeiro erro
            }
            namesConcated += std::format("{} ", r.objectName);
            allVars.insert(allVars.end(), r.localVars.begin(),
                           r.localVars.end());
            hasChanged |= r.hasHeaderChanged;
        }
    }

    if (errorCode != 0) {
        result.error = CompilerError::BuildFailed;
        result.value.returnCode = errorCode;
        return result;
    }

    if (!linked) {
        auto linkerFlags = buildSettings_->getExtraLinkerFlags();

        // Link (sequencial)
        const std::string linkCmd = std::format(
            "{} {} -shared -g -Wl,--export-dynamic {} {} -o lib{}.so", compiler,
            linkerFlags, namesConcated, getLinkLibrariesStr(), libname);

        if (auto linkRes = executeCommand(linkCmd); !linkRes) {
            result.error = CompilerError::LinkingFailed;
            result.value.returnCode = linkRes.value;
            return result;
        }
    }

    const auto t1 = std::chrono::system_clock::now();
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    if (sources.size() == 1) {
        std::cout << std::format(
            "Parallel AST+compile time (single source): {}ms\n", ms);
    } else {
        std::cout << std::format(
            "Parallel AST+compile time ({} sources, max {} threads): {}ms\n",
            sources.size(), getEffectiveThreadCount(), ms);
    }

    if (varMergeCallback_) {
        varMergeCallback_(allVars);
    }
    if (hasChanged) {
        analysis::AstContext::staticSaveHeaderToFile("decl_amalgama.hpp");
    }

    result.value.variables = std::move(allVars);
    result.value.returnCode = 0;
    return result;
}

CompilerResult<std::vector<std::string>> CompilerService::analyzeCustomCommands(
    const std::vector<std::string> &commands) const {

    CompilerResult<std::vector<std::string>> result;

    // Vetor thread-safe para coletar todas as variáveis
    std::vector<std::string> allVars;
    std::mutex varsMutex;

    // Coletor de erros thread-safe
    std::vector<std::string> errors;
    std::mutex errorsMutex;

    auto evalcmd = [&](const std::string &cmd) {
        try {
            // Executa o comando usando executeCommand
            auto cmdResult = executeCommand(cmd);

            if (!cmdResult.success()) {
                std::lock_guard<std::mutex> lock(errorsMutex);
                errors.push_back("Erro ao executar comando: " + cmd);
                return;
            }

            // Processa o JSON de saída se o comando contém -ast-dump
            if (cmd.find("-ast-dump") == std::string::npos) {
                return;
            }

            // Extrai o arquivo JSON do comando
            std::string jsonFile;
            size_t jsonPos = cmd.find(" > ");
            if (jsonPos != std::string::npos) {
                jsonFile = cmd.substr(jsonPos + 3);
                // Remove espaços e quebras de linha
                jsonFile.erase(jsonFile.find_last_not_of(" \n\r\t") + 1);
            }

            if (jsonFile.empty()) {
                return;
            }
            // Usa simdjson para analisar o arquivo

            std::ifstream file(jsonFile);
            if (!file.is_open()) {
                return;
            }

            std::string content((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());

            // Análise com simdjson (similar ao código original)
            simdjson::dom::parser parser;
            simdjson::dom::element doc;
            auto error = parser.parse(content).get(doc);

            if (error != simdjson::SUCCESS) {
                std::lock_guard<std::mutex> lock(errorsMutex);
                errors.push_back("Erro ao analisar JSON: " + jsonFile);
                return;
            }

            // Função recursiva para extrair declarações
            std::function<void(simdjson::dom::element)> extractDecls;
            std::vector<std::string> localVars;

            extractDecls = [&](simdjson::dom::element node) {
                if (!node.is_object()) {
                    return;
                }

                simdjson::dom::object obj = node;

                // Verifica se é uma declaração de
                // variável/função
                auto kind = obj["kind"];
                auto name = obj["name"];

                if (!kind.error() && !name.error()) {
                    std::string_view kindStr = kind;
                    std::string_view nameStr = name;

                    if (kindStr == "VarDecl" || kindStr == "FunctionDecl" ||
                        kindStr == "CXXMethodDecl" || kindStr == "FieldDecl") {
                        localVars.push_back(std::string(nameStr));
                    }
                }

                // Processa filhos recursivamente
                auto inner = obj["inner"];
                if (!inner.error() && inner.is_array()) {
                    for (auto child : inner) {
                        extractDecls(child);
                    }
                }
            };

            extractDecls(doc);

            // Adiciona variáveis encontradas ao resultado
            if (localVars.empty()) {
                return;
            }

            std::lock_guard<std::mutex> lock(varsMutex);
            allVars.insert(allVars.end(), localVars.begin(), localVars.end());
        } catch (const std::exception &e) {
            std::lock_guard<std::mutex> lock(errorsMutex);
            errors.push_back("Exceção durante análise: " +
                             std::string(e.what()));
        }
    };

    // Processa os comandos em paralelo
    std::for_each(std::execution::par_unseq, commands.begin(), commands.end(),
                  evalcmd);

    // Verifica se houve erros
    if (!errors.empty()) {
        result.error = CompilerError::SystemCommandFailed;
        return result;
    }

    // Remove duplicatas
    std::sort(allVars.begin(), allVars.end());
    allVars.erase(std::unique(allVars.begin(), allVars.end()), allVars.end());

    // Chama callback se definido
    if (varMergeCallback_ && !allVars.empty()) {
        // Converte std::string para VarDecl para usar o callback
        std::vector<VarDecl> varDecls;
        for (const auto &var : allVars) {
            VarDecl decl;
            decl.name = var;
            decl.type =
                "auto"; // Tipo genérico já que não temos informação do tipo
            varDecls.push_back(decl);
        }
        varMergeCallback_(varDecls);
    }

    result.error = CompilerError::Success;
    result.value = std::move(allVars);
    return result;
}

} // namespace compiler
