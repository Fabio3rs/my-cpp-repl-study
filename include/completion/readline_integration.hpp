#pragma once

#include "clang_completion.hpp"
#include <memory>
#include <readline/history.h>
#include <readline/readline.h>

// Forward declarations
struct BuildSettings;

namespace completion {

/**
 * @brief Integração entre ClangCompletion e readline
 *
 * Fornece callbacks para o sistema readline usar o autocompletion
 * baseado em libclang com contexto semântico.
 */
class ReadlineIntegration {
  private:
    // Static members for singleton-like behavior
    static std::shared_ptr<ClangCompletion> clangCompletion_;
    static std::vector<std::string> currentCompletions_;
    static std::string currentPrefix_;
    static int referenceCount_;

    // Callbacks estáticos para readline (C API)
    static char **replCompletionFunction(const char *text, int start, int end);
    static char *replCompletionGenerator(const char *text, int state);

    // Helpers para conversão
    static std::vector<std::string>
    extractCompletionTexts(const std::vector<CompletionItem> &items);
    static char *duplicateString(const std::string &str);

  public:
    /**
     * @brief Inicializa a integração readline + clang
     */
    static void initialize();

    /**
     * @brief Inicializa a integração readline + clang com configurações
     */
    static void initialize(const BuildSettings &settings);

    /**
     * @brief Finaliza e limpa recursos
     */
    static void cleanup();

    /**
     * @brief Atualiza o contexto do REPL para autocompletion
     */
    static void updateContext(const ReplContext &context);

    /**
     * @brief Configura callbacks do readline
     */
    static void setupReadlineCallbacks();

    /**
     * @brief Obtem instância do ClangCompletion (para uso interno)
     */
    static ClangCompletion *getClangCompletion();

    /**
     * @brief Verifica se a integração está inicializada
     */
    static bool isInitialized();
};

/**
 * @brief RAII wrapper para gerenciar readline integration
 */
class ReadlineCompletionScope {
  public:
    ReadlineCompletionScope();
    ~ReadlineCompletionScope();

    // Não copiável
    ReadlineCompletionScope(const ReadlineCompletionScope &) = delete;
    ReadlineCompletionScope &
    operator=(const ReadlineCompletionScope &) = delete;
};

/**
 * @brief Utilitários para construção de contexto REPL
 */
namespace context_builder {

/**
 * @brief Constrói contexto a partir do estado atual do REPL
 */
ReplContext buildFromReplState(const std::string &currentInput = "");

/**
 * @brief Extrai includes do código atual
 */
std::string extractIncludes(const std::string &code);

/**
 * @brief Extrai declarações de variáveis do estado
 */
std::string extractVariableDeclarations();

/**
 * @brief Extrai declarações de funções
 */
std::string extractFunctionDeclarations();

/**
 * @brief Determina posição do cursor no código
 */
std::pair<int, int> getCurrentCursorPosition(const std::string &input);

} // namespace context_builder

} // namespace completion
