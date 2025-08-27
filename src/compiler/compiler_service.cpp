#include "compiler/compiler_service.hpp"

#include "../../repl.hpp"
#include "analysis/ast_context.hpp"
#include "analysis/clang_ast_adapter.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <execution>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

// Forward declaration for helper function from repl.cpp
extern auto
runProgramGetOutput(std::string_view cmd) -> std::pair<std::string, int>;

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
    return compiler + " -std=" + std + " " + flags + " " +
           getIncludeDirectoriesStr() + " " + getPreprocessorDefinitionsStr() +
           " " + inputFile + " " + getLinkLibrariesStr() + " -o " + outputFile;
}

CompilerResult<int>
CompilerService::executeCommand(const std::string &command) const {
    std::cout << "Executing: " << command << std::endl;

    int result = system(command.c_str());

    CompilerResult<int> compilerResult;
    compilerResult.value = result;

    if (result != 0) {
        compilerResult.error = CompilerError::SystemCommandFailed;
        std::cerr << "Command failed with code: " << result << std::endl;
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
        concat += name + " ";
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
        content += line + "\n";
    }

    return content;
}

void CompilerService::printCompilationError(const std::string &logPath,
                                            const std::string &context) const {
    std::string errorLog = readLogFile(logPath);

    if (!errorLog.empty()) {
        // Header colorido
        std::cerr << getColorCode("red") << "=== Compilation Error in "
                  << context << " ===" << getColorCode("reset") << std::endl;

        // Processar cada linha do log com cores
        std::istringstream stream(errorLog);
        std::string line;
        while (std::getline(stream, line)) {
            std::cerr << formatErrorLine(line) << std::endl;
        }

        std::cerr << getColorCode("red")
                  << "===============================" << getColorCode("reset")
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
        formatted = getColorCode("red") + getColorCode("bold") + line +
                    getColorCode("reset");
    } else if (line.find("warning:") != std::string::npos) {
        formatted = getColorCode("yellow") + getColorCode("bold") + line +
                    getColorCode("reset");
    } else if (line.find("note:") != std::string::npos) {
        formatted = getColorCode("blue") + line + getColorCode("reset");
    } else if (line.find("^") != std::string::npos &&
               line.find("~") != std::string::npos) {
        // Linha de indicação de erro (setas e tildes)
        formatted = getColorCode("green") + line + getColorCode("reset");
    }

    return formatted;
}

// === Core Operations ===

CompilerResult<int> CompilerService::buildLibraryOnly(
    const std::string &compiler, const std::string &name,
    const std::string &ext, const std::string &std) const {
    std::string includePrecompiledHeader = getPrecompiledHeaderFlag(ext);

    auto cmd = compiler + " -std=" + std + " -shared " +
               includePrecompiledHeader + getIncludeDirectoriesStr() + " " +
               getPreprocessorDefinitionsStr() +
               " -g -Wl,--export-dynamic -fPIC " + name + ext + " " +
               getLinkLibrariesStr() + " -o lib" + name + ".so";

    return executeCommand(cmd);
}

CompilerResult<std::vector<VarDecl>> CompilerService::buildLibraryWithAST(
    const std::string &compiler, const std::string &name,
    const std::string &ext, const std::string &std) const {
    CompilerResult<std::vector<VarDecl>> result;

    std::string includePrecompiledHeader = getPrecompiledHeaderFlag(ext);

    // First build - compile to shared library
    auto cmd = compiler + " -std=" + std + " -shared " +
               includePrecompiledHeader + getIncludeDirectoriesStr() + " " +
               getPreprocessorDefinitionsStr() +
               " -g -Wl,--export-dynamic -fPIC " + name + ext + " " +
               getLinkLibrariesStr() + " -o lib" + name + ".so";

    auto buildResult = executeCommand(cmd);
    if (!buildResult) {
        result.error = CompilerError::BuildFailed;
        return result;
    }

    // Second build - AST dump
    cmd = compiler + " -std=" + std +
          " -fcolor-diagnostics -fPIC -Xclang -ast-dump=json " +
          includePrecompiledHeader + getIncludeDirectoriesStr() + " " +
          getPreprocessorDefinitionsStr() + " -fsyntax-only " + name + ext +
          " -o lib" + name + ".so > " + name + ".json";

    auto astResult = executeCommand(cmd);
    if (!astResult) {
        result.error = CompilerError::AstAnalysisFailed;
        return result;
    }

    // Analyze AST
    analysis::ClangAstAnalyzerAdapter analyzer;
    std::vector<VarDecl> vars;
    int ares = analyzer.analyzeFile(name + ".json", name + ".cpp", vars);
    if (ares != 0) {
        result.error = CompilerError::AstAnalysisFailed;
        return result;
    }

    // Merge variables using callback if provided
    if (varMergeCallback_) {
        varMergeCallback_(vars);
    }

    // Third build - final library with proper settings
    cmd = compiler + getPreprocessorDefinitionsStr() + " " +
          getIncludeDirectoriesStr() +
          " -std=gnu++20 -shared -include precompiledheader.hpp -g "
          "-Wl,--export-dynamic -fPIC " +
          name + ".cpp " + getLinkLibrariesStr() + " -o lib" + name + ".so";

    auto finalBuildResult = executeCommand(cmd);
    if (!finalBuildResult) {
        result.error = CompilerError::BuildFailed;
        return result;
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
        precompHeader << "#include \"printerOutput.hpp\"\n\n" << std::endl;

        // Se temos um contexto, usar os includes dele
        if (contextToUse) {
            precompHeader << contextToUse->getOutputHeader() << std::endl;
        }

        precompHeader.flush();
        precompHeader.close();

    } catch (const std::exception &e) {
        std::cerr << "Failed to write precompiled header: " << e.what()
                  << std::endl;
        result.error = CompilerError::FileWriteFailed;
        return result;
    }

    std::string cmd = compiler + getPreprocessorDefinitionsStr() + " " +
                      getIncludeDirectoriesStr() +
                      " -fPIC -x c++-header -std=gnu++20 -o "
                      "precompiledheader.hpp.pch precompiledheader.hpp";

    auto cmdResult = executeCommand(cmd);
    if (!cmdResult) {
        result.error = CompilerError::PrecompiledHeaderFailed;
        return result;
    }

    return result;
}

CompilerResult<int>
CompilerService::linkObjects(const std::vector<std::string> &objects,
                             const std::string &libname) const {
    std::string linkLibraries = getLinkLibrariesStr();
    std::string namesConcated = concatenateNames(objects);

    std::string cmd = "clang++ -shared -g -WL,--export-dynamic " +
                      namesConcated + " " + linkLibraries + " -o lib" +
                      libname + ".so";

    return executeCommand(cmd);
}

CompilerResult<CompilationResult> CompilerService::buildMultipleSourcesWithAST(
    const std::string &compiler, const std::string &libname,
    const std::vector<std::string> &sources, const std::string &std) const {
    CompilerResult<CompilationResult> result;

    std::string includes = getIncludeDirectoriesStr();
    std::string preprocessorDefinitions = getPreprocessorDefinitionsStr();

    auto now = std::chrono::system_clock::now();

    std::string namesConcated;
    std::vector<VarDecl> allVars;
    int errorCode = 0;

    // Process each source file sequentially (simplified from parallel version)
    for (const auto &name : sources) {
        if (name.empty()) {
            continue;
        }

        const auto path = std::filesystem::path(name);
        std::string purefilename = path.filename().string();
        purefilename = purefilename.substr(0, purefilename.find_last_of('.'));

        // AST Analysis - generate JSON file
        std::string logname = purefilename + ".log";
        std::string jsonFile = purefilename + ".json";
        std::string cmd =
            compiler + preprocessorDefinitions + " " + includes +
            " -std=" + std +
            " -fcolor-diagnostics -fPIC -Xclang -ast-dump=json "
            " -Xclang -include-pch -Xclang precompiledheader.hpp.pch "
            "-include precompiledheader.hpp -fsyntax-only " +
            name + " -o lib" + purefilename + "_blank.so > " + jsonFile +
            " 2>" + logname;

        auto out = runProgramGetOutput(cmd);

        if (out.second != 0) {
            std::cerr << "runProgramGetOutput(cmd) != 0: " << out.second << " "
                      << cmd << std::endl;

            // Print compilation error log
            printCompilationError(logname, "AST dump for " + name);

            errorCode = out.second;
            break;
        }
        analysis::ClangAstAnalyzerAdapter analyzer;
        std::vector<VarDecl> localVars;

        // Analisar o AST usando o saída do comando anterior (JSON do AST)
        int ares = analyzer.analyzeFile(jsonFile, name, localVars);
        if (ares != 0) {
            std::cerr << "AST analysis failed for " << name
                      << " with code: " << ares << std::endl;
            errorCode = ares;
            break;
        }

        allVars.insert(allVars.end(), localVars.begin(), localVars.end());

        // Compile to object
        try {
            std::string object = purefilename + ".o";
            namesConcated += object + " ";

            cmd = compiler + preprocessorDefinitions + " " + includes +
                  " -std=gnu++20 -fPIC -c -Xclang "
                  "-include-pch -Xclang precompiledheader.hpp.pch "
                  "-include precompiledheader.hpp "
                  "-g -fPIC " +
                  name + " -o " + object;

            int buildres = system(cmd.c_str());

            if (buildres != 0) {
                errorCode = buildres;
                std::cerr << "buildres != 0: " << cmd << std::endl;
                std::cout << name << std::endl;

                // Print compilation error log
                printCompilationError(logname,
                                      "object compilation for " + name);

                break;
            }
        } catch (const std::exception &e) {
            std::cerr << e.what() << std::endl;
            errorCode = -1;
            break;
        }
    }

    if (errorCode != 0) {
        result.error = CompilerError::BuildFailed;
        result.value.returnCode = errorCode;
        return result;
    }

    auto end = std::chrono::system_clock::now();
    std::cout << "Analyze and compile time: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(end -
                                                                       now)
                     .count()
              << "ms\n";

    // Link all objects
    std::string cmd = compiler + " -shared -g -WL,--export-dynamic " +
                      namesConcated + " " + getLinkLibrariesStr() + " -o lib" +
                      libname + ".so";

    auto linkResult = executeCommand(cmd);
    if (!linkResult) {
        result.error = CompilerError::LinkingFailed;
        result.value.returnCode = linkResult.value;
        return result;
    }

    // Merge variables using callback if provided
    if (varMergeCallback_) {
        varMergeCallback_(allVars);
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

    // Processa os comandos em paralelo
    std::for_each(
        std::execution::par_unseq, commands.begin(), commands.end(),
        [&](const std::string &cmd) {
            try {
                // Executa o comando usando executeCommand
                auto cmdResult = executeCommand(cmd);

                if (!cmdResult.success()) {
                    std::lock_guard<std::mutex> lock(errorsMutex);
                    errors.push_back("Erro ao executar comando: " + cmd);
                    return;
                }

                // Processa o JSON de saída se o comando contém -ast-dump
                if (cmd.find("-ast-dump") != std::string::npos) {
                    // Extrai o arquivo JSON do comando
                    std::string jsonFile;
                    size_t jsonPos = cmd.find(" > ");
                    if (jsonPos != std::string::npos) {
                        jsonFile = cmd.substr(jsonPos + 3);
                        // Remove espaços e quebras de linha
                        jsonFile.erase(jsonFile.find_last_not_of(" \n\r\t") +
                                       1);
                    }

                    if (!jsonFile.empty()) {
                        // Usa simdjson para analisar o arquivo
                        std::ifstream file(jsonFile);
                        if (file.is_open()) {
                            std::string content(
                                (std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());

                            // Análise com simdjson (similar ao código original)
                            simdjson::dom::parser parser;
                            simdjson::dom::element doc;
                            auto error = parser.parse(content).get(doc);

                            if (!error) {
                                // Função recursiva para extrair declarações
                                std::function<void(simdjson::dom::element)>
                                    extractDecls;
                                std::vector<std::string> localVars;

                                extractDecls =
                                    [&](simdjson::dom::element node) {
                                        if (node.is_object()) {
                                            simdjson::dom::object obj = node;

                                            // Verifica se é uma declaração de
                                            // variável/função
                                            auto kind = obj["kind"];
                                            auto name = obj["name"];

                                            if (!kind.error() &&
                                                !name.error()) {
                                                std::string_view kindStr = kind;
                                                std::string_view nameStr = name;

                                                if (kindStr == "VarDecl" ||
                                                    kindStr == "FunctionDecl" ||
                                                    kindStr ==
                                                        "CXXMethodDecl" ||
                                                    kindStr == "FieldDecl") {
                                                    localVars.push_back(
                                                        std::string(nameStr));
                                                }
                                            }

                                            // Processa filhos recursivamente
                                            auto inner = obj["inner"];
                                            if (!inner.error() &&
                                                inner.is_array()) {
                                                for (auto child : inner) {
                                                    extractDecls(child);
                                                }
                                            }
                                        }
                                    };

                                extractDecls(doc);

                                // Adiciona variáveis encontradas ao resultado
                                if (!localVars.empty()) {
                                    std::lock_guard<std::mutex> lock(varsMutex);
                                    allVars.insert(allVars.end(),
                                                   localVars.begin(),
                                                   localVars.end());
                                }
                            } else {
                                std::lock_guard<std::mutex> lock(errorsMutex);
                                errors.push_back("Erro ao analisar JSON: " +
                                                 jsonFile);
                            }
                        }
                    }
                }

            } catch (const std::exception &e) {
                std::lock_guard<std::mutex> lock(errorsMutex);
                errors.push_back("Exceção durante análise: " +
                                 std::string(e.what()));
            }
        });

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
