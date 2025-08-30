#include "completion/readline_integration.hpp"
#include "../../repl.hpp" // Para BuildSettings
#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>

namespace completion {

// Definições estáticas
std::shared_ptr<ClangCompletion> ReadlineIntegration::clangCompletion_ =
    nullptr;
std::vector<std::string> ReadlineIntegration::currentCompletions_;
std::string ReadlineIntegration::currentPrefix_;
int ReadlineIntegration::referenceCount_ = 0;

void ReadlineIntegration::initialize() {
    referenceCount_++;

    if (!clangCompletion_) {
        clangCompletion_ = std::make_shared<ClangCompletion>();
        clangCompletion_->initialize({});
        setupReadlineCallbacks();
    }
}

void ReadlineIntegration::initialize(const BuildSettings &settings) {
    referenceCount_++;

    if (!clangCompletion_) {
        clangCompletion_ = std::make_shared<ClangCompletion>();
        clangCompletion_->initialize(settings);
        setupReadlineCallbacks();
    }
}

void ReadlineIntegration::cleanup() {
    if (referenceCount_ > 0) {
        referenceCount_--;
    }

    if (referenceCount_ == 0 && clangCompletion_) {
        clangCompletion_.reset();
        currentCompletions_.clear();
        currentPrefix_.clear();
        std::cout << "[DEBUG] ReadlineIntegration: Cleaned up successfully\n";
    } else if (referenceCount_ > 0) {
        std::cout << "[DEBUG] ReadlineIntegration: Still has "
                  << referenceCount_ << " references, keeping alive\n";
    }
}

void ReadlineIntegration::updateContext(const ReplContext &context) {
    std::cout << "[DEBUG] ReadlineIntegration: updateContext() called\n";

    if (clangCompletion_) {
        clangCompletion_->updateReplContext(context);
    } else {
        std::cout << "[WARNING] ReadlineIntegration: Not initialized, cannot "
                     "update context\n";
    }
}

void ReadlineIntegration::setupReadlineCallbacks() {
    std::cout
        << "[DEBUG] ReadlineIntegration: setupReadlineCallbacks() called\n";

    // Configurar readline para usar nosso sistema de completion
    rl_attempted_completion_function = replCompletionFunction;

    // Configurar caracteres que quebram palavras para completion
    rl_completer_word_break_characters =
        const_cast<char *>(" \t\n\"\\'`@$><=;|&{(");

    std::cout << "[DEBUG] ReadlineIntegration: Callbacks configured\n";
}

char **ReadlineIntegration::replCompletionFunction(const char *text, int start,
                                                   int end) {
    std::cout << "[DEBUG] replCompletionFunction called: text='" << text
              << "', start=" << start << ", end=" << end << "\n";

    // Previne readline de usar completion padrão
    rl_attempted_completion_over = 1;

    if (!clangCompletion_) {
        std::cout << "[WARNING] ClangCompletion not initialized\n";
        return nullptr;
    }

    // Obter linha completa sendo editada
    std::string fullLine(rl_line_buffer);
    std::string prefix(text);

    std::cout << "[DEBUG] Full line: '" << fullLine << "'\n";
    std::cout << "[DEBUG] Prefix: '" << prefix << "'\n";

    // Determinar posição no código (simplificado para o mock)
    int line = 1;
    int column = end + 1;

    // Obter completions do clang
    auto completions = clangCompletion_->getCompletions(prefix, line, column);

    // Converter para formato readline
    currentCompletions_ = extractCompletionTexts(completions);
    currentPrefix_ = prefix;

    if (currentCompletions_.empty()) {
        std::cout << "[DEBUG] No completions found\n";
        return nullptr;
    }

    // Criar array de strings para readline
    char **matches = static_cast<char **>(
        malloc(sizeof(char *) * (currentCompletions_.size() + 1)));
    if (!matches) {
        return nullptr;
    }

    for (size_t i = 0; i < currentCompletions_.size(); ++i) {
        matches[i] = duplicateString(currentCompletions_[i]);
    }
    matches[currentCompletions_.size()] = nullptr;

    std::cout << "[DEBUG] Returning " << currentCompletions_.size()
              << " matches\n";
    return matches;
}

char *ReadlineIntegration::replCompletionGenerator(const char *text,
                                                   int state) {
    std::cout << "[DEBUG] replCompletionGenerator: text='" << text
              << "', state=" << state << "\n";

    // Esta função não é usada em nossa implementação atual
    // (usamos replCompletionFunction diretamente)
    return nullptr;
}

std::vector<std::string> ReadlineIntegration::extractCompletionTexts(
    const std::vector<CompletionItem> &items) {

    std::vector<std::string> texts;
    texts.reserve(items.size());

    for (const auto &item : items) {
        texts.push_back(item.text);
    }

    std::cout << "[DEBUG] Extracted " << texts.size() << " completion texts\n";
    return texts;
}

char *ReadlineIntegration::duplicateString(const std::string &str) {
    char *dup = static_cast<char *>(malloc(str.length() + 1));
    if (dup) {
        strcpy(dup, str.c_str());
    }
    return dup;
}

ClangCompletion *ReadlineIntegration::getClangCompletion() {
    return clangCompletion_.get();
}

bool ReadlineIntegration::isInitialized() {
    return clangCompletion_ != nullptr;
}

// ReadlineCompletionScope Implementation

ReadlineCompletionScope::ReadlineCompletionScope() {
    std::cout << "[DEBUG] ReadlineCompletionScope: Constructor\n";
    ReadlineIntegration::initialize();
}

ReadlineCompletionScope::~ReadlineCompletionScope() {
    std::cout << "[DEBUG] ReadlineCompletionScope: Destructor\n";
    ReadlineIntegration::cleanup();
}

// Context Builder Implementation

namespace context_builder {

ReplContext buildFromReplState(const std::string &currentInput) {
    std::cout << "[DEBUG] buildFromReplState called with input: '"
              << currentInput << "'\n";

    ReplContext context;

    // TODO: Integrar com o estado real do REPL
    context.currentIncludes = extractIncludes("");
    context.variableDeclarations = extractVariableDeclarations();
    context.functionDeclarations = extractFunctionDeclarations();
    context.activeCode = currentInput;

    auto [line, column] = getCurrentCursorPosition(currentInput);
    context.line = line;
    context.column = column;

    std::cout << "[DEBUG] Built context: includes="
              << context.currentIncludes.size()
              << " chars, vars=" << context.variableDeclarations.size()
              << " chars\n";

    return context;
}

std::string extractIncludes(const std::string &code) {
    // TODO: Extrair includes reais do código/estado
    std::ostringstream oss;
    oss << "#include <iostream>\n";
    oss << "#include <string>\n";
    oss << "#include <vector>\n";

    std::cout << "[DEBUG] extractIncludes: Mock includes added\n";
    return oss.str();
}

std::string extractVariableDeclarations() {
    // TODO: Integrar com replState.varsNames
    std::ostringstream oss;

    // Mock variables baseado no que poderia estar no REPL
    oss << "int myVar = 42;\n";
    oss << "std::string myString = \"hello\";\n";
    oss << "std::vector<int> myVector = {1, 2, 3};\n";

    std::cout << "[DEBUG] extractVariableDeclarations: Mock variables added\n";
    return oss.str();
}

std::string extractFunctionDeclarations() {
    // TODO: Integrar com funções definidas no REPL
    std::ostringstream oss;

    // Mock functions
    oss << "void myFunction();\n";
    oss << "int calculate(int a, int b);\n";

    std::cout << "[DEBUG] extractFunctionDeclarations: Mock functions added\n";
    return oss.str();
}

std::pair<int, int> getCurrentCursorPosition(const std::string &input) {
    // Calcular posição baseada no input atual
    int line = 1;
    int column = 1;

    for (char c : input) {
        if (c == '\n') {
            line++;
            column = 1;
        } else {
            column++;
        }
    }

    std::cout << "[DEBUG] getCurrentCursorPosition: line=" << line
              << ", column=" << column << "\n";
    return {line, column};
}

} // namespace context_builder

} // namespace completion
