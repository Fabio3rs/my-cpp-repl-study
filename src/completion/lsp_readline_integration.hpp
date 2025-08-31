#pragma once

#include "../../repl.hpp" // Para CompilerCodeCfg e BuildSettings
#include "LspClangdService.hpp"
#include "completion/completion_types.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <readline/history.h>
#include <readline/readline.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace completion {

/**
 * @brief Integração do sistema de completion usando LSP/clangd via readline
 *
 * Esta implementação moderna substitui o sistema baseado em libclang direto,
 * oferecendo melhor performance e precisão através do clangd LSP server.
 *
 * Características:
 * - Performance superior com índices persistentes do clangd
 * - Análise semântica completa, não apenas sintática
 * - Cache inteligente de preambles e contexto
 * - Diagnostics em tempo real
 * - Suporte completo para C++20/23
 */
class LspReadlineIntegration {
  public:
    struct Config {
        std::string tempDir = "/tmp/cpprepl";
        std::string virtualFilePrefix = "repl_buffer_";
        int preambleTimeoutMs = 2000;   // Timeout para build do preamble
        int completionTimeoutMs = 1000; // Timeout para completions
        int maxCompletions = 100;       // Limite de completions retornadas
        bool enableDiagnostics = true;  // Mostrar erros em tempo real
        bool verboseLogging = false;    // Debug detalhado

        // Configurações do clangd
        LspClangdService::Options clangdOpts;
    };

    /**
     * @brief RAII scope para gerenciar lifetime da integração
     */
    class Scope {
      public:
        explicit Scope(const Config &config);
        Scope(); // Construtor padrão
        ~Scope();

        // Atualizar contexto do REPL
        void updateReplContext(const ReplContext &context);

        // Obter instância ativa
        static LspReadlineIntegration *getInstance();

      private:
        std::unique_ptr<LspReadlineIntegration> integration_;
        static LspReadlineIntegration *activeInstance_;
    };

  private:
    // Construtor privado - usar apenas via Scope
    explicit LspReadlineIntegration(const Config &config);

  public:
    ~LspReadlineIntegration();

    // Não copiável/movível
    LspReadlineIntegration(const LspReadlineIntegration &) = delete;
    LspReadlineIntegration &operator=(const LspReadlineIntegration &) = delete;

    /**
     * @brief Inicializar o serviço LSP e configurar readline
     */
    bool initialize(const CompilerCodeCfg &codeCfg,
                    const BuildSettings &buildSettings);

    /**
     * @brief Atualizar contexto do REPL (variáveis, includes, código anterior)
     */
    void updateContext(const ReplContext &context);

    /**
     * @brief Obter completions para uma posição específica
     */
    std::vector<CompletionItem> getCompletions(const std::string &code,
                                               int line, int column);

    /**
     * @brief Obter diagnostics para o código atual
     */
    std::vector<DiagnosticInfo> getDiagnostics(const std::string &code);

    /**
     * @brief Shutdown limpo do serviço
     */
    void shutdown();

  private:
    // Configuração
    Config config_;

    // Serviço LSP
    std::unique_ptr<LspClangdService> lspService_;

    // Estado do contexto
    ReplContext currentContext_;
    std::string currentBufferUri_;
    int currentVersion_;
    std::string lastKnownCode_;

    // Cache e otimizações
    struct CacheEntry {
        std::string code;
        std::vector<CompletionItem> completions;
        std::chrono::steady_clock::time_point timestamp;
        int line, column;
    };
    std::unordered_map<std::string, CacheEntry> completionCache_;
    std::chrono::milliseconds cacheValidityMs_{500};

    // Diagnostics callback
    std::vector<DiagnosticInfo> lastDiagnostics_;

    // Callbacks estáticos do readline
    static char **readlineCompletionFunction(const char *text, int start,
                                             int end);
    static char *readlineGenerator(const char *text, int state);

    // Estado para callbacks estáticos
    static std::vector<std::string> currentMatches_;
    static size_t currentMatchIndex_;

    // Implementação interna
    bool initializeLspService(const CompilerCodeCfg &codeCfg,
                              const BuildSettings &buildSettings);
    void setupReadlineCallbacks();
    std::string buildCompleteCode(const std::string &currentInput) const;
    std::string generateVirtualUri() const;
    void updateLspBuffer(const std::string &code);
    bool waitForPreamble(const std::string &uri, int timeoutMs);
    void onDiagnosticsReceived(const nlohmann::json &params);
    std::vector<CompletionItem>
    parseCompletionResponse(const nlohmann::json &response);
    std::vector<DiagnosticInfo> parseDiagnostics(const nlohmann::json &params);
    bool isCacheValid(const CacheEntry &entry, const std::string &code,
                      int line, int column) const;
    void cleanExpiredCache();
    void logDebug(const std::string &message) const;
    void logError(const std::string &message) const;
};

/**
 * @brief Builder de contexto para integração com estado do REPL
 */
namespace lsp_context_builder {

/**
 * @brief Construir contexto completo do REPL para o LSP
 */
ReplContext buildFromReplState(const std::string &currentInput,
                               const std::vector<std::string> &includes = {},
                               const std::vector<std::string> &variables = {},
                               const std::vector<std::string> &functions = {});

/**
 * @brief Extrair includes do histórico de comandos
 */
std::vector<std::string>
extractIncludesFromHistory(const std::vector<std::string> &history);

/**
 * @brief Construir declarações de variáveis do estado do REPL
 */
std::string
buildVariableDeclarations(const std::vector<std::string> &variables);

/**
 * @brief Construir declarações de funções definidas no REPL
 */
std::string
buildFunctionDeclarations(const std::vector<std::string> &functions);

/**
 * @brief Calcular posição exata do cursor no código completo
 */
std::pair<int, int> calculateCursorPosition(const std::string &fullCode,
                                            const std::string &currentInput,
                                            int inputCursorPos);

} // namespace lsp_context_builder

} // namespace completion
