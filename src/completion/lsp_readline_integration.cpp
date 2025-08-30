#include "lsp_readline_integration.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unistd.h>

namespace completion {

// Definições estáticas
LspReadlineIntegration *LspReadlineIntegration::Scope::activeInstance_ =
    nullptr;
std::vector<std::string> LspReadlineIntegration::currentMatches_;
size_t LspReadlineIntegration::currentMatchIndex_ = 0;

// ============================================================================
// LspReadlineIntegration::Scope Implementation
// ============================================================================

LspReadlineIntegration::Scope::Scope(const Config &config) {
    if (activeInstance_) {
        throw std::runtime_error("LspReadlineIntegration already active - only "
                                 "one instance allowed");
    }

    integration_ = std::unique_ptr<LspReadlineIntegration>(
        new LspReadlineIntegration(config));
    activeInstance_ = integration_.get();

    std::cout << "[DEBUG] LspReadlineIntegration::Scope created\n";
}

LspReadlineIntegration::Scope::Scope() : Scope(Config{}) {
    // Delegating constructor - usa configuração padrão
}

LspReadlineIntegration::Scope::~Scope() {
    if (integration_) {
        integration_->shutdown();
    }
    activeInstance_ = nullptr;
    std::cout << "[DEBUG] LspReadlineIntegration::Scope destroyed\n";
}

void LspReadlineIntegration::Scope::updateReplContext(
    const ReplContext &context) {
    if (integration_) {
        integration_->updateContext(context);
    }
}

LspReadlineIntegration *LspReadlineIntegration::Scope::getInstance() {
    return activeInstance_;
}

// ============================================================================
// LspReadlineIntegration Implementation
// ============================================================================

LspReadlineIntegration::LspReadlineIntegration(const Config &config)
    : config_(config), lspService_(std::make_unique<LspClangdService>()),
      currentVersion_(0) {
    // Criar diretório temporário se não existir
    std::filesystem::create_directories(config_.tempDir);

    logDebug("LspReadlineIntegration constructed");
}

LspReadlineIntegration::~LspReadlineIntegration() {
    shutdown();
    logDebug("LspReadlineIntegration destroyed");
}

bool LspReadlineIntegration::initialize(const CompilerCodeCfg &codeCfg,
                                        const BuildSettings &buildSettings) {
    logDebug("Initializing LspReadlineIntegration...");

    if (!initializeLspService(codeCfg, buildSettings)) {
        logError("Failed to initialize LSP service");
        return false;
    }

    setupReadlineCallbacks();

    logDebug("LspReadlineIntegration initialized successfully");
    return true;
}

void LspReadlineIntegration::updateContext(const ReplContext &context) {
    logDebug("Updating REPL context...");

    currentContext_ = context;

    // Construir código completo com novo contexto
    std::string fullCode = buildCompleteCode(context.activeCode);

    // Atualizar buffer no LSP se mudou
    if (fullCode != lastKnownCode_) {
        updateLspBuffer(fullCode);
        lastKnownCode_ = fullCode;

        // Limpar cache - contexto mudou
        completionCache_.clear();
    }
}

std::vector<CompletionItem>
LspReadlineIntegration::getCompletions(const std::string &code, int line,
                                       int column) {
    logDebug("Getting completions at line=" + std::to_string(line) +
             ", column=" + std::to_string(column));

    // Verificar cache primeiro
    std::string cacheKey = std::to_string(line) + ":" + std::to_string(column) +
                           ":" + std::to_string(std::hash<std::string>{}(code));

    auto cacheIt = completionCache_.find(cacheKey);
    if (cacheIt != completionCache_.end() &&
        isCacheValid(cacheIt->second, code, line, column)) {
        logDebug("Using cached completions (" +
                 std::to_string(cacheIt->second.completions.size()) +
                 " items)");
        return cacheIt->second.completions;
    }

    if (!lspService_) {
        logError("LSP service not initialized");
        return {};
    }

    // Atualizar buffer com código atual
    std::string fullCode = buildCompleteCode(code);
    updateLspBuffer(fullCode);

    try {
        // Solicitar completion do clangd (posições são 0-based no LSP)
        auto response =
            lspService_->completion(currentBufferUri_, line - 1, column - 1);

        auto completions = parseCompletionResponse(response);

        // Limitar número de completions
        if (completions.size() > static_cast<size_t>(config_.maxCompletions)) {
            completions.resize(config_.maxCompletions);
        }

        // Atualizar cache
        CacheEntry entry{.code = code,
                         .completions = completions,
                         .timestamp = std::chrono::steady_clock::now(),
                         .line = line,
                         .column = column};
        completionCache_[cacheKey] = std::move(entry);

        // Limpeza periódica do cache
        cleanExpiredCache();

        logDebug("Retrieved " + std::to_string(completions.size()) +
                 " completions from LSP");
        return completions;

    } catch (const std::exception &e) {
        logError("Exception during completion: " + std::string(e.what()));
        return {};
    }
}

std::vector<DiagnosticInfo>
LspReadlineIntegration::getDiagnostics(const std::string &code) {
    if (!config_.enableDiagnostics || !lspService_) {
        return {};
    }

    // Atualizar buffer e aguardar diagnostics
    std::string fullCode = buildCompleteCode(code);
    updateLspBuffer(fullCode);

    // Os diagnostics chegam via callback assíncrono
    return lastDiagnostics_;
}

void LspReadlineIntegration::shutdown() {
    logDebug("Shutting down LspReadlineIntegration...");

    if (lspService_) {
        lspService_->shutdown();
        lspService_.reset();
    }

    // Limpar cache
    completionCache_.clear();
    lastDiagnostics_.clear();
    currentMatches_.clear();

    logDebug("LspReadlineIntegration shutdown complete");
}

// ============================================================================
// Private Implementation
// ============================================================================

bool LspReadlineIntegration::initializeLspService(
    const CompilerCodeCfg &codeCfg, const BuildSettings &buildSettings) {
    logDebug("Initializing LSP service...");

    // Configurar LSP service
    lspService_->configure(codeCfg, buildSettings, config_.clangdOpts);

    // Configurar callback de diagnostics
    lspService_->onDiagnostics = [this](const nlohmann::json &params) {
        onDiagnosticsReceived(params);
    };

    // Iniciar clangd
    if (!lspService_->start()) {
        logError("Failed to start clangd LSP service");
        return false;
    }

    // Gerar URI para o buffer virtual
    currentBufferUri_ = generateVirtualUri();

    logDebug("LSP service initialized with URI: " + currentBufferUri_);
    return true;
}

void LspReadlineIntegration::setupReadlineCallbacks() {
    logDebug("Setting up readline callbacks...");

    // Configurar completion function
    rl_attempted_completion_function = readlineCompletionFunction;

    // Configurar caracteres que quebram palavras
    rl_completer_word_break_characters =
        const_cast<char *>(" \t\n\"'`@$><=;|&{(");

    // Configurar caracteres de quote
    rl_completer_quote_characters = const_cast<char *>("\"'");

    logDebug("Readline callbacks configured");
}

std::string LspReadlineIntegration::buildCompleteCode(
    const std::string &currentInput) const {
    std::ostringstream oss;

    // Adicionar includes do contexto
    oss << currentContext_.currentIncludes << "\n";

    // Adicionar declarações de variáveis
    if (!currentContext_.variableDeclarations.empty()) {
        oss << "// REPL Variables\n";
        oss << currentContext_.variableDeclarations << "\n";
    }

    // Adicionar declarações de funções
    if (!currentContext_.functionDeclarations.empty()) {
        oss << "// REPL Functions\n";
        oss << currentContext_.functionDeclarations << "\n";
    }

    // Adicionar código atual em uma função principal
    oss << "// Current REPL Code\n";
    oss << "int main() {\n";
    oss << currentInput << "\n";
    oss << "return 0;\n";
    oss << "}\n";

    return oss.str();
}

std::string LspReadlineIntegration::generateVirtualUri() const {
    static int counter = 0;
    return "file://" + config_.tempDir + "/" + config_.virtualFilePrefix +
           std::to_string(++counter) + ".cpp";
}

void LspReadlineIntegration::updateLspBuffer(const std::string &code) {
    if (!lspService_)
        return;

    try {
        if (currentVersion_ == 0) {
            // Primeira vez - abrir buffer
            lspService_->openBuffer(currentBufferUri_, "cpp", code,
                                    ++currentVersion_);

            // Aguardar preamble ser processado
            waitForPreamble(currentBufferUri_, config_.preambleTimeoutMs);
        } else {
            // Atualizar buffer existente
            lspService_->changeBuffer(currentBufferUri_, code,
                                      ++currentVersion_);
        }

        logDebug("LSP buffer updated (version " +
                 std::to_string(currentVersion_) + ")");

    } catch (const std::exception &e) {
        logError("Failed to update LSP buffer: " + std::string(e.what()));
    }
}

bool LspReadlineIntegration::waitForPreamble(const std::string &uri,
                                             int timeoutMs) {
    logDebug("Waiting for preamble processing...");

    bool success = lspService_->waitDiagnostics(uri, timeoutMs);

    if (success) {
        logDebug("Preamble processed successfully");
    } else {
        logError("Timeout waiting for preamble processing");
    }

    return success;
}

void LspReadlineIntegration::onDiagnosticsReceived(
    const nlohmann::json &params) {
    if (!config_.enableDiagnostics)
        return;

    try {
        lastDiagnostics_ = parseDiagnostics(params);

        if (config_.verboseLogging) {
            logDebug("Received " + std::to_string(lastDiagnostics_.size()) +
                     " diagnostics");
        }

    } catch (const std::exception &e) {
        logError("Failed to parse diagnostics: " + std::string(e.what()));
    }
}

std::vector<CompletionItem> LspReadlineIntegration::parseCompletionResponse(
    const nlohmann::json &response) {
    std::vector<CompletionItem> items;

    try {
        if (!response.contains("result")) {
            logError("Completion response missing 'result' field");
            return items;
        }

        const auto &result = response["result"];
        nlohmann::json itemsArray;

        if (result.is_array()) {
            itemsArray = result;
        } else if (result.is_object() && result.contains("items")) {
            itemsArray = result["items"];
        } else {
            logError("Invalid completion response format");
            return items;
        }

        for (const auto &item : itemsArray) {
            if (!item.contains("label"))
                continue;

            CompletionItem completion;
            completion.text = item["label"].get<std::string>();

            // Extrair informações adicionais se disponíveis
            if (item.contains("detail")) {
                completion.signature = item["detail"].get<std::string>();
            }

            if (item.contains("documentation")) {
                const auto &doc = item["documentation"];
                if (doc.is_string()) {
                    completion.documentation = doc.get<std::string>();
                } else if (doc.is_object() && doc.contains("value")) {
                    completion.documentation = doc["value"].get<std::string>();
                }
            }

            // Mapear kind do LSP para nosso tipo
            if (item.contains("kind")) {
                int kind = item["kind"].get<int>();
                switch (kind) {
                case 3:
                    completion.kind = CompletionItem::Kind::Function;
                    break;
                case 5:
                    completion.kind = CompletionItem::Kind::Field;
                    break;
                case 6:
                    completion.kind = CompletionItem::Kind::Variable;
                    break;
                case 7:
                    completion.kind = CompletionItem::Kind::Class;
                    break;
                case 9:
                    completion.kind = CompletionItem::Kind::Module;
                    break;
                case 14:
                    completion.kind = CompletionItem::Kind::Keyword;
                    break;
                default:
                    completion.kind = CompletionItem::Kind::Variable;
                    break;
                }
            }

            // Priority baseada no tipo e se começa com underscore
            completion.priority = (completion.text.front() == '_') ? 5 : 1;

            items.push_back(std::move(completion));
        }

    } catch (const std::exception &e) {
        logError("Exception parsing completion response: " +
                 std::string(e.what()));
    }

    return items;
}

std::vector<DiagnosticInfo>
LspReadlineIntegration::parseDiagnostics(const nlohmann::json &params) {
    std::vector<DiagnosticInfo> diagnostics;

    try {
        if (!params.contains("diagnostics")) {
            return diagnostics;
        }

        for (const auto &diag : params["diagnostics"]) {
            DiagnosticInfo info;

            if (diag.contains("message")) {
                info.message = diag["message"].get<std::string>();
            }

            if (diag.contains("severity")) {
                int severity = diag["severity"].get<int>();
                switch (severity) {
                case 1:
                    info.severity = DiagnosticInfo::Severity::Error;
                    break;
                case 2:
                    info.severity = DiagnosticInfo::Severity::Warning;
                    break;
                case 3:
                    info.severity = DiagnosticInfo::Severity::Information;
                    break;
                case 4:
                    info.severity = DiagnosticInfo::Severity::Hint;
                    break;
                default:
                    info.severity = DiagnosticInfo::Severity::Information;
                    break;
                }
            }

            if (diag.contains("range")) {
                const auto &range = diag["range"];
                if (range.contains("start")) {
                    const auto &start = range["start"];
                    info.range.start.line = start.value("line", 0);
                    info.range.start.character = start.value("character", 0);
                }
                if (range.contains("end")) {
                    const auto &end = range["end"];
                    info.range.end.line = end.value("line", 0);
                    info.range.end.character = end.value("character", 0);
                }
            }

            diagnostics.push_back(std::move(info));
        }

    } catch (const std::exception &e) {
        logError("Exception parsing diagnostics: " + std::string(e.what()));
    }

    return diagnostics;
}

bool LspReadlineIntegration::isCacheValid(const CacheEntry &entry,
                                          const std::string &code, int line,
                                          int column) const {
    auto now = std::chrono::steady_clock::now();
    auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - entry.timestamp);

    return age < cacheValidityMs_ && entry.code == code && entry.line == line &&
           entry.column == column;
}

void LspReadlineIntegration::cleanExpiredCache() {
    auto now = std::chrono::steady_clock::now();

    auto it = completionCache_.begin();
    while (it != completionCache_.end()) {
        auto age = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - it->second.timestamp);
        if (age > cacheValidityMs_) {
            it = completionCache_.erase(it);
        } else {
            ++it;
        }
    }
}

void LspReadlineIntegration::logDebug(const std::string &message) const {
    if (config_.verboseLogging) {
        std::cout << "[DEBUG] LspReadlineIntegration: " << message << "\n";
    }
}

void LspReadlineIntegration::logError(const std::string &message) const {
    std::cerr << "[ERROR] LspReadlineIntegration: " << message << "\n";
}

// ============================================================================
// Readline Callbacks (Static)
// ============================================================================

char **LspReadlineIntegration::readlineCompletionFunction(const char *text,
                                                          int start, int end) {
    // Prevenir completion padrão do readline
    rl_attempted_completion_over = 1;

    auto *instance = Scope::getInstance();
    if (!instance) {
        return nullptr;
    }

    try {
        std::string fullLine(rl_line_buffer ? rl_line_buffer : "");
        std::string prefix(text ? text : "");

        instance->logDebug("Completion requested: text='" + prefix +
                           "', start=" + std::to_string(start) +
                           ", end=" + std::to_string(end));

        // Calcular posição no código
        int line = 1;
        int column = end + 1;

        // TODO: Calcular posição mais precisa baseada no contexto completo
        for (int i = 0; i < end && i < static_cast<int>(fullLine.length());
             ++i) {
            if (fullLine[i] == '\n') {
                line++;
                column = 1;
            } else {
                column++;
            }
        }

        // Obter completions
        auto completions = instance->getCompletions(fullLine, line, column);

        // Converter para formato readline
        currentMatches_.clear();
        currentMatches_.reserve(completions.size());

        for (const auto &completion : completions) {
            currentMatches_.push_back(completion.text);
        }

        if (currentMatches_.empty()) {
            return nullptr;
        }

        // Criar array de strings para readline
        char **matches = static_cast<char **>(
            malloc(sizeof(char *) * (currentMatches_.size() + 1)));
        if (!matches) {
            return nullptr;
        }

        for (size_t i = 0; i < currentMatches_.size(); ++i) {
            matches[i] = strdup(currentMatches_[i].c_str());
        }
        matches[currentMatches_.size()] = nullptr;

        instance->logDebug("Returning " +
                           std::to_string(currentMatches_.size()) +
                           " completions");
        return matches;

    } catch (const std::exception &e) {
        if (instance) {
            instance->logError("Exception in completion callback: " +
                               std::string(e.what()));
        }
        return nullptr;
    }
}

char *LspReadlineIntegration::readlineGenerator(const char *text, int state) {
    // Esta função não é usada em nossa implementação
    // (usamos readlineCompletionFunction diretamente)
    return nullptr;
}

// ============================================================================
// Context Builder Implementation
// ============================================================================

namespace lsp_context_builder {

ReplContext buildFromReplState(const std::string &currentInput,
                               const std::vector<std::string> &includes,
                               const std::vector<std::string> &variables,
                               const std::vector<std::string> &functions) {
    ReplContext context;

    // Construir includes
    std::ostringstream includesStream;
    for (const auto &include : includes) {
        includesStream << "#include " << include << "\n";
    }
    context.currentIncludes = includesStream.str();

    // Construir declarações
    context.variableDeclarations = buildVariableDeclarations(variables);
    context.functionDeclarations = buildFunctionDeclarations(functions);
    context.activeCode = currentInput;

    // Calcular posição do cursor (simplificado - final do input)
    auto [line, column] = calculateCursorPosition(
        context.currentIncludes + context.variableDeclarations +
            context.functionDeclarations + currentInput,
        currentInput, currentInput.length());
    context.line = line;
    context.column = column;

    return context;
}

std::vector<std::string>
extractIncludesFromHistory(const std::vector<std::string> &history) {
    std::vector<std::string> includes;

    for (const auto &line : history) {
        // Procurar por padrões de include
        if (line.find("#include") == 0) {
            includes.push_back(line);
        }
    }

    return includes;
}

std::string
buildVariableDeclarations(const std::vector<std::string> &variables) {
    std::ostringstream oss;

    for (const auto &var : variables) {
        oss << var << ";\n";
    }

    return oss.str();
}

std::string
buildFunctionDeclarations(const std::vector<std::string> &functions) {
    std::ostringstream oss;

    for (const auto &func : functions) {
        oss << func << ";\n";
    }

    return oss.str();
}

std::pair<int, int> calculateCursorPosition(const std::string &fullCode,
                                            const std::string &currentInput,
                                            int inputCursorPos) {
    int line = 0;
    int column = 0;
    // LSP começa em 0
    // Encontrar onde o currentInput começa no fullCode
    size_t inputStart = fullCode.find(currentInput);
    if (inputStart == std::string::npos) {
        // Fallback: posição no final do código
        for (char c : fullCode) {
            if (c == '\n') {
                line++;
                column = 1;
            } else {
                column++;
            }
        }
        return {line, column};
    }

    // Calcular posição até o início do input
    for (size_t i = 0; i < inputStart; ++i) {
        if (fullCode[i] == '\n') {
            line++;
            column = 0;
        } else {
            column++;
        }
    }

    // Adicionar posição dentro do input atual
    for (int i = 0;
         i < inputCursorPos && i < static_cast<int>(currentInput.length());
         ++i) {
        if (currentInput[i] == '\n') {
            line++;
            column = 0;
        } else {
            column++;
        }
    }

    return {line, column};
}

} // namespace lsp_context_builder

} // namespace completion
