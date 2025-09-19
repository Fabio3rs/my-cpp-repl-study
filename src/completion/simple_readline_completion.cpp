#include "completion/simple_readline_completion.hpp"
#include "commands/command_registry.hpp"
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

    keywords_.clear();
    replCommands_.clear();
    variables_.clear();
    functions_.clear();

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
    // Populate replCommands_ from the central commands registry so that
    // completion reflects all registered REPL commands (including plugins
    // and dynamically registered ones).
    try {
        const auto &entries = commands::registry().entries();
        for (const auto &e : entries) {
            replCommands_.emplace(e.prefix);
        }
    } catch (...) {
        // Fallback to a small static set if registry access fails for any
        // reason (defensive programming)
        constexpr const char *replCmds[] = {
            "#help",       "#welcome", "#eval",       "#lazyeval", "#return",
            "#batch_eval", "#include", "#includedir", "#lib",      "#link",
            "#define",     "#undef",   "printall",    "evalall",   "exit"};
        for (const auto *cmd : replCmds) {
            replCommands_.emplace(cmd);
        }
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
// Generator used by readline's rl_completion_matches. Returns a strdup()'d
// string or nullptr when exhausted. readline will free returned strings.
char *SimpleReadlineCompletion::completion_generator(const char *text,
                                                     int state) noexcept {
    static size_t idx = 0;
    if (state == 0) {
        idx = 0;
    }

    if (!activeInstance_)
        return nullptr;

    if (idx >= currentMatches_.size())
        return nullptr;

    const std::string &s = currentMatches_[idx++];
    return strdup(s.c_str());
}

char **SimpleReadlineCompletion::completionFunction(const char *text, int start,
                                                    int end) noexcept {
    // Let readline handle single-tab vs double-tab behavior; provide
    // candidates via rl_completion_matches.
    rl_attempted_completion_over = 1;

    if (!activeInstance_ || !text) {
        return nullptr;
    }

    std::string_view prefix(text);
    currentMatches_ = activeInstance_->getCompletions(prefix);

    if (currentMatches_.empty()) {
        return nullptr;
    }

    if (verbosityLevel >= 3) {
        std::cout << "[DEBUG] completionFunction(prefix='" << text
                  << "', start=" << start << ", end=" << end << ") -> "
                  << currentMatches_.size() << " matches\n";
    }

    return rl_completion_matches(text, completion_generator);
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
