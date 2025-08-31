#include "completion/simple_readline_completion.hpp"
#include "repl.hpp" // Para ReplState e VarDecl
#include <algorithm>
#include <cstring>
#include <iostream>

// Forward declaration da variável global definida em repl.cpp
extern int verbosityLevel;

namespace completion {

// Static members
SimpleReadlineCompletion *SimpleReadlineCompletion::activeInstance_ = nullptr;
std::vector<std::string> SimpleReadlineCompletion::currentMatches_;

void SimpleReadlineCompletion::initialize() noexcept {
    activeInstance_ = this;

    // Configurar readline callbacks
    rl_attempted_completion_function = completionFunction;
    rl_completer_word_break_characters =
        const_cast<char *>(" \t\n\"'`@$><=;|&{(");

    addBuiltinKeywords();
    addReplCommands();
}

void SimpleReadlineCompletion::shutdown() noexcept {
    if (activeInstance_ == this) {
        activeInstance_ = nullptr;
        rl_attempted_completion_function = nullptr;
    }
    currentMatches_.clear();
}

void SimpleReadlineCompletion::updateFromReplState(
    const ReplState &state) noexcept {
    // Limpar dados anteriores (manter keywords built-in)
    variables_.clear();
    functions_.clear();

    // Adicionar variáveis do REPL
    for (const auto &varName : state.varsNames) {
        variables_.emplace(varName);
    }

    // Adicionar variáveis detalhadas
    for (const auto &var : state.allTheVariables) {
        if (var.kind == "VarDecl") {
            variables_.emplace(var.name);
        } else if (var.kind == "FunctionDecl") {
            functions_.emplace(var.name);
        }
    }

    // Adicionar funções dos printer addresses
    for (const auto &[funcName, _] : state.varPrinterAddresses) {
        functions_.emplace(funcName);
    }

    if (verbosityLevel >= 3) {
        std::cout << "[DEBUG] SimpleCompletion: Updated context - "
                  << variables_.size() << " vars, " << functions_.size()
                  << " funcs\n";
    }
}

void SimpleReadlineCompletion::addBuiltinKeywords() noexcept {
    // C++ keywords essenciais
    constexpr const char *cppKeywords[] = {
        "auto", "const", "constexpr", "static", "inline", "extern", "if",
        "else", "for", "while", "do", "switch", "case", "default", "return",
        "break", "continue", "goto", "try", "catch", "throw", "noexcept", "new",
        "delete", "sizeof", "alignof", "decltype", "typeid", "nullptr", "true",
        "false", "class", "struct", "enum", "union", "namespace", "using",
        "typedef", "public", "private", "protected", "virtual", "override",
        "final", "template", "typename",

        // Types básicos
        "void", "bool", "char", "int", "float", "double", "long", "short",
        "signed", "unsigned", "wchar_t", "char16_t", "char32_t",

        // STL comum
        "std::cout", "std::cin", "std::cerr", "std::endl", "std::string",
        "std::vector", "std::array", "std::map", "std::set",
        "std::unordered_map", "std::unordered_set", "std::unique_ptr",
        "std::shared_ptr", "std::weak_ptr", "std::make_unique",
        "std::make_shared", "std::function", "std::optional", "std::variant",
        "std::chrono", "std::thread", "std::mutex", "std::lock_guard"};

    for (const auto *keyword : cppKeywords) {
        keywords_.emplace(keyword);
    }
}

void SimpleReadlineCompletion::addReplCommands() noexcept {
    constexpr const char *replCmds[] = {
        "#help",       "#welcome", "#eval",       "#lazyeval", "#return",
        "#batch_eval", "#include", "#includedir", "#lib",      "#link",
        "#define",     "#undef",   "printall",    "evalall",   "exit"};

    for (const auto *cmd : replCmds) {
        replCommands_.emplace(cmd);
    }
}

std::vector<std::string> SimpleReadlineCompletion::getCompletions(
    std::string_view prefix) const noexcept {
    std::vector<std::string> matches;
    matches.reserve(64);

    // Helper lambda para adicionar matches
    auto addMatches = [&](const std::unordered_set<std::string> &container) {
        for (const auto &item : container) {
            if (item.starts_with(prefix)) {
                matches.push_back(item);
            }
        }
    };

    // Buscar em todas as categorias
    addMatches(variables_);
    addMatches(functions_);
    addMatches(keywords_);
    addMatches(replCommands_);

    // Ordenar por relevância: variáveis primeiro, depois funções, etc.
    std::sort(matches.begin(), matches.end(),
              [&](const auto &a, const auto &b) {
                  // Prioridade: exact match > variables > functions > commands
                  // > keywords
                  bool aIsVar = variables_.contains(a);
                  bool bIsVar = variables_.contains(b);
                  bool aIsFunc = functions_.contains(a);
                  bool bIsFunc = functions_.contains(b);
                  bool aIsCmd = replCommands_.contains(a);
                  bool bIsCmd = replCommands_.contains(b);

                  // Exact matches primeiro
                  if (a == prefix && b != prefix)
                      return true;
                  if (b == prefix && a != prefix)
                      return false;

                  // Variáveis do REPL têm prioridade
                  if (aIsVar && !bIsVar)
                      return true;
                  if (bIsVar && !aIsVar)
                      return false;

                  // Funções do REPL em segundo
                  if (aIsFunc && !bIsFunc)
                      return true;
                  if (bIsFunc && !aIsFunc)
                      return false;

                  // Comandos REPL em terceiro
                  if (aIsCmd && !bIsCmd)
                      return true;
                  if (bIsCmd && !aIsCmd)
                      return false;

                  // Mesmo tipo: ordenação alfabética
                  return a < b;
              });

    // Remover duplicatas mantendo ordem
    auto last = std::unique(matches.begin(), matches.end());
    matches.erase(last, matches.end());

    // Limitar resultados para performance
    constexpr size_t maxResults = 50;
    if (matches.size() > maxResults) {
        matches.resize(maxResults);
    }

    return matches;
}

// Static callback implementation
char **SimpleReadlineCompletion::completionFunction(const char *text, int start,
                                                    int end) noexcept {
    // Prevenir completion padrão do readline
    rl_attempted_completion_over = 1;

    if (!activeInstance_ || !text) {
        return nullptr;
    }

    std::string_view prefix(text);
    currentMatches_ = activeInstance_->getCompletions(prefix);

    if (currentMatches_.empty()) {
        return nullptr;
    }

    // Alocar array de strings para readline
    char **matches = static_cast<char **>(
        malloc(sizeof(char *) * (currentMatches_.size() + 1)));

    if (!matches) {
        return nullptr;
    }

    for (size_t i = 0; i < currentMatches_.size(); ++i) {
        matches[i] = strdup(currentMatches_[i].c_str());
        if (!matches[i]) {
            // Cleanup em caso de falha
            for (size_t j = 0; j < i; ++j) {
                free(matches[j]);
            }
            free(matches);
            return nullptr;
        }
    }
    matches[currentMatches_.size()] = nullptr;

    if (verbosityLevel >= 3) {
        std::cout << "[DEBUG] Completion: Found " << currentMatches_.size()
                  << " matches for '" << text << "'\n";
    }

    return matches;
}

// RAII Wrapper Implementation
SimpleCompletionScope::SimpleCompletionScope(
    const ReplState &replState) noexcept {
    completion_.initialize();
    completion_.updateFromReplState(replState);
}

SimpleCompletionScope::~SimpleCompletionScope() noexcept {
    completion_.shutdown();
}

void SimpleCompletionScope::updateContext(const ReplState &state) noexcept {
    completion_.updateFromReplState(state);
}

} // namespace completion
