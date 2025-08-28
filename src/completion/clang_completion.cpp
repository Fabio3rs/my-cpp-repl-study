#include "completion/clang_completion.hpp"
#include "../repl.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace completion {

ClangCompletion::ClangCompletion()
    : index_(nullptr), translationUnit_(nullptr) {
    // TODO: Inicializar libclang quando disponível
    // initializeClang();
    std::cout
        << "[DEBUG] ClangCompletion: Constructor (libclang não disponível)\n";
}

ClangCompletion::~ClangCompletion() {
    // cleanupClang();
    std::cout << "[DEBUG] ClangCompletion: Destructor\n";
}

void ClangCompletion::initializeClang() {
    // TODO: Implementar quando libclang estiver disponível
    /*
    index_ = clang_createIndex(0, 0);
    if (!index_) {
        throw std::runtime_error("Failed to create clang index");
    }
    */
    std::cout << "[DEBUG] ClangCompletion: initializeClang() called\n";
}

void ClangCompletion::cleanupClang() {
    // TODO: Implementar cleanup
    /*
    if (translationUnit_) {
        clang_disposeTranslationUnit(translationUnit_);
        translationUnit_ = nullptr;
    }
    if (index_) {
        clang_disposeIndex(index_);
        index_ = nullptr;
    }
    */
    std::cout << "[DEBUG] ClangCompletion: cleanupClang() called\n";
}

void ClangCompletion::updateReplContext(const ReplContext &context) {
    replContext_ = context;

    // Invalidar translation unit atual para forçar recriação
    if (translationUnit_) {
        // clang_disposeTranslationUnit(translationUnit_);
        translationUnit_ = nullptr;
    }

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

    // TODO: Implementar com libclang real
    std::vector<CompletionItem> mockCompletions;

    // Mock completions baseado no contexto atual
    if (partialCode.find("std::") != std::string::npos) {
        mockCompletions.push_back(
            {"vector", "std::vector<T>", "Dynamic array container",
             "template<class T>", 10, CompletionItem::Kind::Class});
        mockCompletions.push_back({"string", "std::string", "String class",
                                   "class", 10, CompletionItem::Kind::Class});
        mockCompletions.push_back({"cout", "std::cout", "Console output stream",
                                   "ostream&", 8,
                                   CompletionItem::Kind::Variable});
    }

    // Simular completions de variáveis do contexto
    if (!replContext_.variableDeclarations.empty()) {
        mockCompletions.push_back({"myVar", "myVar",
                                   "User-defined variable from REPL", "int", 15,
                                   CompletionItem::Kind::Variable});
    }

    // Keyword completions básicos
    if (partialCode.empty() || std::isalpha(partialCode[0])) {
        std::vector<std::string> keywords = {
            "int",   "float",  "double", "char",      "bool",  "auto",
            "const", "static", "inline", "namespace", "class", "struct"};

        for (const auto &keyword : keywords) {
            if (keyword.find(partialCode) == 0) { // Starts with partialCode
                mockCompletions.push_back({keyword, keyword, "C++ keyword", "",
                                           5, CompletionItem::Kind::Keyword});
            }
        }
    }

    // Ordenar por prioridade (maior prioridade primeiro)
    std::sort(mockCompletions.begin(), mockCompletions.end(),
              [](const CompletionItem &a, const CompletionItem &b) {
                  return a.priority > b.priority;
              });

    std::cout << "[DEBUG] Found " << mockCompletions.size() << " completions\n";
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
    oss << partialCode; // Código parcial na posição do cursor
    oss << "\nreturn 0;\n}";

    return oss.str();
}

std::string ClangCompletion::getDocumentation(const std::string &symbol) {
    std::cout << "[DEBUG] ClangCompletion: getDocumentation('" << symbol
              << "')\n";

    // TODO: Usar libclang para obter documentação real
    std::unordered_map<std::string, std::string> mockDocs = {
        {"vector", "std::vector<T> - Dynamic array container that can resize "
                   "automatically"},
        {"string", "std::string - Class for handling strings of characters"},
        {"cout", "std::cout - Standard output stream for console output"},
        {"push_back", "Adds element to the end of the container"},
        {"size", "Returns the number of elements in the container"}};

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

std::vector<CompletionItem>
ClangCompletion::parseClangCompletions(CXCodeCompleteResults *results) const {
    std::vector<CompletionItem> items;

    // TODO: Implementar parsing real dos resultados do libclang
    /*
    for (unsigned i = 0; i < results->NumResults; ++i) {
        CXCompletionResult& result = results->Results[i];

        CompletionItem item;
        // Parse completion string
        CXCompletionString completionString = result.CompletionString;

        // Extract text, documentation, etc.
        // ...

        items.push_back(std::move(item));
    }
    */

    std::cout << "[DEBUG] ClangCompletion: parseClangCompletions() mock "
                 "implementation\n";
    return items;
}

CompletionItem::Kind ClangCompletion::mapClangKind(CXCursorKind kind) const {
    // TODO: Mapear tipos do clang para nossos tipos
    /*
    switch (kind) {
        case CXCursor_VarDecl: return CompletionItem::Kind::Variable;
        case CXCursor_FunctionDecl: return CompletionItem::Kind::Function;
        case CXCursor_StructDecl: return CompletionItem::Kind::Struct;
        case CXCursor_ClassDecl: return CompletionItem::Kind::Class;
        default: return CompletionItem::Kind::Variable;
    }
    */

    (void)kind; // Suppress warning
    return CompletionItem::Kind::Variable;
}

} // namespace completion
