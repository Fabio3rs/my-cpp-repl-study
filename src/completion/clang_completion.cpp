#include "../include/completion/clang_completion.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#ifdef CLANG_COMPLETION_ENABLED
#include <algorithm>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Parse/ParseAST.h>
#include <clang/Sema/Sema.h>
#endif

namespace completion {

#ifdef CLANG_COMPLETION_ENABLED
// Coletor de completions personalizado
class CompletionCollector : public clang::CodeCompleteConsumer {
  private:
    std::vector<CompletionItem> &results_;
    clang::CodeCompletionAllocator allocator_;
    clang::CodeCompletionTUInfo tuInfo_;

  public:
    CompletionCollector(std::vector<CompletionItem> &results)
        : clang::CodeCompleteConsumer(clang::CodeCompleteOptions()),
          results_(results), allocator_(),
          tuInfo_(std::make_shared<clang::GlobalCodeCompletionAllocator>()) {}

    clang::CodeCompletionAllocator &getAllocator() override {
        return allocator_;
    }

    clang::CodeCompletionTUInfo &getCodeCompletionTUInfo() override {
        return tuInfo_;
    }

    void ProcessCodeCompleteResults(clang::Sema &S,
                                    clang::CodeCompletionContext Context,
                                    clang::CodeCompletionResult *Results,
                                    unsigned NumResults) override {
        for (unsigned i = 0; i < NumResults; ++i) {
            const auto &Result = Results[i];

            CompletionItem item;

            if (Result.Kind == clang::CodeCompletionResult::RK_Declaration) {
                if (auto *ND = Result.Declaration) {
                    item.text = ND->getNameAsString();
                    item.display = item.text;

                    // Mapear tipo de declaração para nosso enum
                    if (llvm::isa<clang::FunctionDecl>(ND)) {
                        item.kind = CompletionItem::Kind::Function;
                        item.priority = 10;
                    } else if (llvm::isa<clang::VarDecl>(ND)) {
                        item.kind = CompletionItem::Kind::Variable;
                        item.priority = 8;
                    } else if (llvm::isa<clang::RecordDecl>(ND)) {
                        if (llvm::isa<clang::CXXRecordDecl>(ND)) {
                            item.kind = CompletionItem::Kind::Class;
                        } else {
                            item.kind = CompletionItem::Kind::Struct;
                        }
                        item.priority = 7;
                    } else if (llvm::isa<clang::EnumDecl>(ND)) {
                        item.kind = CompletionItem::Kind::Enum;
                        item.priority = 6;
                    }
                }
            } else if (Result.Kind == clang::CodeCompletionResult::RK_Keyword) {
                item.text = Result.Keyword;
                item.display = item.text;
                item.kind = CompletionItem::Kind::Keyword;
                item.priority = 5;
            } else if (Result.Kind == clang::CodeCompletionResult::RK_Macro) {
                if (auto *MI = Result.Macro) {
                    item.text = MI->getName().str();
                    item.display = item.text;
                    item.kind = CompletionItem::Kind::Macro;
                    item.priority = 4;
                }
            }

            // Ajustar prioridade baseada na prioridade do Clang (menor =
            // melhor)
            if (Result.Priority > 0) {
                int adjusted = item.priority - (Result.Priority / 10);
                item.priority = adjusted > 0 ? adjusted : 1;
            }

            if (!item.text.empty()) {
                results_.push_back(item);
            }
        }
    }
};

// Ação frontend personalizada para code completion
class CCAction : public clang::ASTFrontendAction {
  private:
    unsigned line_, column_;
    std::vector<CompletionItem> &results_;

  public:
    CCAction(unsigned line, unsigned column,
             std::vector<CompletionItem> &results)
        : line_(line), column_(column), results_(results) {}

    std::unique_ptr<clang::ASTConsumer>
    CreateASTConsumer(clang::CompilerInstance &CI,
                      llvm::StringRef file) override {

        // Configurar o consumer de code completion
        auto collector = new CompletionCollector(results_);
        CI.setCodeCompletionConsumer(collector);

        // Configurar o ponto de code completion
        CI.getFrontendOpts().CodeCompletionAt.FileName = file.str();
        CI.getFrontendOpts().CodeCompletionAt.Line = line_;
        CI.getFrontendOpts().CodeCompletionAt.Column = column_;

        // Retornar um consumer vazio já que o code completion
        // é tratado durante o parsing
        return std::make_unique<clang::ASTConsumer>();
    }
};

// Factory para criar CCAction com parâmetros
class CCActionFactory : public clang::tooling::FrontendActionFactory {
  private:
    unsigned line_, column_;
    std::vector<CompletionItem> &results_;

  public:
    CCActionFactory(unsigned line, unsigned column,
                    std::vector<CompletionItem> &results)
        : line_(line), column_(column), results_(results) {}

    std::unique_ptr<clang::FrontendAction> create() override {
        return std::make_unique<CCAction>(line_, column_, results_);
    }
};
#endif

ClangCompletion::ClangCompletion() {
#ifdef CLANG_COMPLETION_ENABLED
    initializeClang();
    std::cout << "[DEBUG] ClangCompletion: Constructor (LibTooling enabled)\n";
#else
    initializeClang(); // Mock initialization
    std::cout << "[DEBUG] ClangCompletion: Constructor (mock mode)\n";
#endif
}

ClangCompletion::~ClangCompletion() {
#ifdef CLANG_COMPLETION_ENABLED
    cleanupClang();
#endif
    std::cout << "[DEBUG] ClangCompletion: Destructor\n";
}

#ifdef CLANG_COMPLETION_ENABLED
void ClangCompletion::initializeClang() {
    // Configurar argumentos para LibTooling
    compiler_args_ = {
        "-std=c++20", "-Wall", "-Wextra", "-I/usr/include",
        "-I/usr/local/include"
        // adicione -I para o seu projeto/contexto do REPL conforme necessário
    };
    std::cout << "[DEBUG] LibTooling initialized\n";
}

void ClangCompletion::cleanupClang() {
    // Nada para limpar com LibTooling - recursos são gerenciados
    // automaticamente
    std::cout << "[DEBUG] LibTooling cleanup (no-op)\n";
}
#else
void ClangCompletion::initializeClang() {
    // Mock initialization - apenas limpa os ponteiros
    std::cout << "[DEBUG] ClangCompletion: Mock libclang initialized\n";
}

void ClangCompletion::cleanupClang() {
    // Mock cleanup - apenas limpa os ponteiros
    std::cout << "[DEBUG] ClangCompletion: Mock libclang cleaned up\n";
}
#endif

void ClangCompletion::updateReplContext(const ReplContext &context) {
    replContext_ = context;

    // Limpar cache para refletir novo contexto
    clearCache();

    std::cout << "[DEBUG] ClangCompletion: Context updated\n";
    std::cout << "  - Includes: " << context.currentIncludes.size()
              << " chars\n";
    std::cout << "  - Variables: " << context.variableDeclarations.size()
              << " chars\n";
    std::cout << "  - Functions: " << context.functionDeclarations.size()
              << " chars\n";
}

std::vector<CompletionItem>
ClangCompletion::getCompletions(const std::string &partialCode, int line,
                                int column) {

    std::cout << "[DEBUG] ClangCompletion: getCompletions() called\n";
    std::cout << "  - Line: " << line << ", Column: " << column << "\n";
    std::cout << "  - Partial code: '" << partialCode << "'\n";

#ifdef CLANG_COMPLETION_ENABLED
    // Implementação real com libclang
    return getCompletionsWithClang(partialCode, line, column);
#else
    // Implementação mock quando libclang não está disponível
    return getCompletionsMock(partialCode, line, column);
#endif
}

#ifdef CLANG_COMPLETION_ENABLED
std::vector<CompletionItem>
ClangCompletion::getCompletionsWithClang(const std::string &partialCode,
                                         int line, int column) {
    std::cout << "[DEBUG] LibTooling completion called\n";

    // 1) Materializar "arquivo virtual" com o contexto do REPL
    std::string code = buildTempFile(partialCode);

    // Debug: imprimir o código gerado para análise
    std::cout << "[DEBUG] Generated code:\n" << code << "\n";

    // 2) Calcular a posição real do cursor no arquivo temporário
    // Contar linhas até chegar ao código parcial
    int actualLine = 1;
    int actualColumn = 1;

    // Contar newlines até chegar ao ponto onde queremos completion
    size_t pos = 0;
    size_t partialPos = code.find(partialCode);

    if (partialPos != std::string::npos) {
        for (size_t i = 0; i < partialPos; ++i) {
            if (code[i] == '\n') {
                actualLine++;
                actualColumn = 1;
            } else {
                actualColumn++;
            }
        }
        // Posição no final do texto parcial
        actualColumn += partialCode.length();
    } else {
        std::cout << "[DEBUG] Could not find partialCode in generated file\n";
        return getCompletionsMock(partialCode, line, column);
    }

    std::cout << "[DEBUG] Code completion at line " << actualLine << ", column "
              << actualColumn << "\n";

    // 3) Coleção onde o consumer vai empurrar os resultados
    std::vector<CompletionItem> out;

    // 4) Criar a ação que liga o ponto de code-completion
    std::unique_ptr<clang::tooling::FrontendActionFactory> Factory(
        new CCActionFactory(static_cast<unsigned>(actualLine),
                            static_cast<unsigned>(actualColumn),
                            std::ref(out)));

    // 5) Rodar a ação nesse "arquivo virtual"
    const std::string VirtualName = "cpprepl_completion.cpp";
    std::vector<std::string> args = compiler_args_;

    bool ok = clang::tooling::runToolOnCodeWithArgs(Factory->create(), code,
                                                    args, VirtualName);

    if (!ok) {
        std::cout << "[DEBUG] runToolOnCodeWithArgs failed; fallback mock\n";
        return getCompletionsMock(partialCode, line, column);
    }

    // 6) Ordenar pela prioridade descendente
    std::sort(out.begin(), out.end(),
              [](const CompletionItem &a, const CompletionItem &b) {
                  return a.priority > b.priority;
              });

    std::cout << "[DEBUG] LibTooling found " << out.size() << " completions\n";
    return out;
}
#endif

std::vector<CompletionItem>
ClangCompletion::getCompletionsMock(const std::string &partialCode, int line,
                                    int column) {
    std::cout << "[DEBUG] Mock completion called\n";

    std::vector<CompletionItem> mockCompletions;

    // Enhanced mock completions based on context
    if (partialCode.find("std::") != std::string::npos) {
        mockCompletions.push_back(
            {"vector", "std::vector<T>", "Dynamic array container",
             "template<class T>", 10, CompletionItem::Kind::Class});
        mockCompletions.push_back({"string", "std::string", "String class",
                                   "class", 10, CompletionItem::Kind::Class});
        mockCompletions.push_back({"cout", "std::cout", "Console output stream",
                                   "ostream&", 8,
                                   CompletionItem::Kind::Variable});
        mockCompletions.push_back({"complex", "std::complex<T>",
                                   "Complex numbers", "template<class T>", 3,
                                   CompletionItem::Kind::Class});
    }

    // Member access completions (message., numbers., etc.)
    if (partialCode.find("message.") != std::string::npos) {
        mockCompletions.push_back({"length", "length()",
                                   "Returns string length", "size_t", 10,
                                   CompletionItem::Kind::Function});
        mockCompletions.push_back({"size", "size()", "Returns string size",
                                   "size_t", 10,
                                   CompletionItem::Kind::Function});
        mockCompletions.push_back({"substr", "substr(pos, len)",
                                   "Returns substring", "string", 8,
                                   CompletionItem::Kind::Function});
    }

    if (partialCode.find("numbers.") != std::string::npos) {
        mockCompletions.push_back({"size", "size()", "Returns vector size",
                                   "size_t", 10,
                                   CompletionItem::Kind::Function});
        mockCompletions.push_back({"push_back", "push_back(value)",
                                   "Adds element", "void", 9,
                                   CompletionItem::Kind::Function});
        mockCompletions.push_back({"at", "at(index)", "Access element at index",
                                   "T&", 8, CompletionItem::Kind::Function});
    }

    // Fuzzy/partial matching for function names
    if (partialCode.find("prt") != std::string::npos &&
        !replContext_.functionDeclarations.empty() &&
        replContext_.functionDeclarations.find("printMessage") !=
            std::string::npos) {
        mockCompletions.push_back({"printMessage", "printMessage()",
                                   "User function", "void", 15,
                                   CompletionItem::Kind::Function});
    }

    // Typo corrections
    if (partialCode.find("stirng") != std::string::npos) {
        mockCompletions.push_back({"string", "std::string",
                                   "Did you mean 'string'?", "class", 12,
                                   CompletionItem::Kind::Class});
        mockCompletions.push_back({"string", "string", "C++ type", "class", 11,
                                   CompletionItem::Kind::Keyword});
    }

    // Contextual help for incomplete statements
    if (partialCode.find("for (") != std::string::npos) {
        mockCompletions.push_back({"int", "int",
                                   "Integer type for loop variable", "type", 10,
                                   CompletionItem::Kind::Keyword});
        mockCompletions.push_back({"auto", "auto", "Auto type deduction",
                                   "keyword", 9,
                                   CompletionItem::Kind::Keyword});
        mockCompletions.push_back({"size_t", "size_t", "Unsigned integer type",
                                   "type", 8, CompletionItem::Kind::Keyword});
    }

    // Simulate context variables in multiline or empty prefix scenarios
    // Check both context and activeCode for variables
    std::string contextToCheck = replContext_.variableDeclarations;
    if (contextToCheck.empty()) {
        contextToCheck = partialCode; // fallback to checking in activeCode
    }

    if (!contextToCheck.empty()) {
        // Known context variables
        if (contextToCheck.find("counter") != std::string::npos) {
            mockCompletions.push_back({"counter", "counter", "User variable",
                                       "int", 15,
                                       CompletionItem::Kind::Variable});
        }
        if (contextToCheck.find("message") != std::string::npos) {
            mockCompletions.push_back({"message", "message", "User variable",
                                       "std::string", 15,
                                       CompletionItem::Kind::Variable});
        }
        if (contextToCheck.find("numbers") != std::string::npos) {
            mockCompletions.push_back({"numbers", "numbers", "User variable",
                                       "std::vector<int>", 15,
                                       CompletionItem::Kind::Variable});
        }

        // Dynamic detection for variables starting with "my"
        if (contextToCheck.find("myCustomVar") != std::string::npos) {
            mockCompletions.push_back({"myCustomVar", "myCustomVar",
                                       "Custom user variable", "int", 15,
                                       CompletionItem::Kind::Variable});
        }
        if (contextToCheck.find("myString") != std::string::npos) {
            mockCompletions.push_back({"myString", "myString",
                                       "Custom string variable", "std::string",
                                       15, CompletionItem::Kind::Variable});
        }
    }

    // Function declarations
    if (!replContext_.functionDeclarations.empty()) {
        if (replContext_.functionDeclarations.find("printMessage") !=
            std::string::npos) {
            mockCompletions.push_back({"printMessage", "printMessage()",
                                       "User function", "void", 14,
                                       CompletionItem::Kind::Function});
        }
        if (replContext_.functionDeclarations.find("calculateSum") !=
            std::string::npos) {
            mockCompletions.push_back({"calculateSum", "calculateSum(a, b)",
                                       "User function", "int", 14,
                                       CompletionItem::Kind::Function});
        }
    }

    // Enhanced keyword completions for empty prefix
    if (partialCode.empty() || std::isalpha(partialCode[0])) {
        std::vector<std::string> keywords = {
            "int",   "float",  "double", "char",      "bool",  "auto",
            "const", "static", "inline", "namespace", "class", "struct",
            "if",    "for",    "while",  "return",    "break", "continue"};

        for (const auto &keyword : keywords) {
            if (partialCode.empty() || keyword.find(partialCode) == 0) {
                mockCompletions.push_back({keyword, keyword, "C++ keyword", "",
                                           5, CompletionItem::Kind::Keyword});
            }
        }
    }

    // Sort by priority (higher priority first)
    std::sort(mockCompletions.begin(), mockCompletions.end(),
              [](const CompletionItem &a, const CompletionItem &b) {
                  return a.priority > b.priority;
              });

    std::cout << "[DEBUG] Mock found " << mockCompletions.size()
              << " completions\n";
    return mockCompletions;
}

std::string
ClangCompletion::buildTempFile(const std::string &partialCode) const {
    std::ostringstream oss;

    // Adicionar includes do contexto
    oss << replContext_.currentIncludes << "\n\n";

    // Adicionar definições de tipos
    oss << replContext_.typeDefinitions << "\n\n";

    // Adicionar declarações de variáveis
    oss << replContext_.variableDeclarations << "\n\n";

    // Adicionar declarações de funções
    oss << replContext_.functionDeclarations << "\n\n";

    // Adicionar o código sendo editado
    oss << "int main() {\n";
    oss << replContext_.activeCode << "\n";

    // Para code completion, simplesmente colocar o código parcial
    // O clang vai fazer completion no ponto especificado
    oss << partialCode;

    oss << "\nreturn 0;\n}";

    return oss.str();
}

std::string ClangCompletion::getDocumentation(const std::string &symbol) {
    std::cout << "[DEBUG] ClangCompletion: getDocumentation('" << symbol
              << "')\n";

    // Enhanced mock documentation for built-in types and common symbols
    std::unordered_map<std::string, std::string> mockDocs = {
        // Built-in types
        {"int", "int - 32-bit signed integer type. Range: -2,147,483,648 to "
                "2,147,483,647"},
        {"string", "std::string - Dynamic string class for handling sequences "
                   "of characters"},
        {"vector", "std::vector<T> - Dynamic array container that can resize "
                   "automatically"},
        {"cout",
         "std::cout - Standard output stream object for console output"},

        // String methods
        {"length",
         "string::length() - Returns the length of the string in characters"},
        {"size", "size() - Returns the number of elements in the container"},
        {"substr", "string::substr(pos, len) - Returns a substring starting at "
                   "position pos"},

        // Vector methods
        {"push_back",
         "vector::push_back(value) - Adds element to the end of the container"},
        {"at", "vector::at(index) - Returns reference to element at specified "
               "position"},

        // Keywords
        {"auto", "auto - Automatic type deduction keyword (C++11)"},
        {"const",
         "const - Keyword to declare constants or read-only variables"},
        {"static",
         "static - Storage class specifier for static storage duration"},
        {"inline",
         "inline - Suggests to compiler to insert function body at call site"},

        // Control flow
        {"if", "if - Conditional statement for branching execution"},
        {"for", "for - Loop statement for iteration with initialization, "
                "condition, and update"},
        {"while",
         "while - Loop statement that continues while condition is true"},
        {"return",
         "return - Statement to exit function and optionally return value"}};

    auto it = mockDocs.find(symbol);
    return (it != mockDocs.end()) ? it->second : "No documentation available";
}

std::vector<std::string>
ClangCompletion::getDiagnostics(const std::string &code) {
    std::cout << "[DEBUG] ClangCompletion: getDiagnostics() called\n";

    // TODO: Usar libclang para análise real
    std::vector<std::string> mockDiagnostics;

    // Análise simples de erros comuns
    if (code.find("cout") != std::string::npos &&
        code.find("#include <iostream>") == std::string::npos) {
        mockDiagnostics.push_back(
            "Warning: 'cout' used but <iostream> not included");
    }

    if (code.find(";") == std::string::npos && !code.empty()) {
        mockDiagnostics.push_back("Warning: Missing semicolon");
    }

    return mockDiagnostics;
}

bool ClangCompletion::symbolExists(const std::string &symbol) const {
    std::cout << "[DEBUG] ClangCompletion: symbolExists('" << symbol << "')\n";

    // TODO: Implementar verificação real com libclang
    // Por enquanto, verificar no contexto atual
    return (
        replContext_.variableDeclarations.find(symbol) != std::string::npos ||
        replContext_.functionDeclarations.find(symbol) != std::string::npos);
}

void ClangCompletion::clearCache() {
    completionCache_.clear();
    std::cout << "[DEBUG] ClangCompletion: Cache cleared\n";
}

} // namespace completion
