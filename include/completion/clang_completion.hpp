#pragma once

// Forward declarations para libclang (quando disponível)
#ifdef CLANG_COMPLETION_ENABLED
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Sema/CodeCompleteConsumer.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/CommandLine.h>

// Forward declaration para nossa ação customizada
namespace completion {
class CompletionCollector;
class CCAction;
} // namespace completion
#endif

#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations
struct VarDecl;

namespace completion {

/**
 * @brief Item de autocompletion com informações contextuais
 */
struct CompletionItem {
    std::string text;          // Texto a ser completado
    std::string display;       // Texto para mostrar ao usuário
    std::string documentation; // Documentação da função/variável
    std::string returnType;    // Tipo de retorno (para funções)
    int priority{};            // Prioridade de ordenação

    enum class Kind {
        Variable,
        Function,
        Class,
        Struct,
        Enum,
        Keyword,
        Include,
        Macro
    } kind{Kind::Variable};
};

/**
 * @brief Context do REPL para análise semântica
 */
struct ReplContext {
    std::string currentIncludes;      // #include statements
    std::string variableDeclarations; // Variáveis declaradas
    std::string functionDeclarations; // Funções declaradas
    std::string typeDefinitions;      // Structs/classes definidas
    std::string activeCode;           // Código sendo editado

    // Posição atual do cursor
    int line = 1;
    int column = 1;
};

/**
 * @brief Sistema de autocompletion usando libclang
 *
 * Integra com o contexto do REPL para fornecer autocompletion
 * semanticamente preciso baseado no estado atual da sessão.
 */
class ClangCompletion {
  private:
    ReplContext replContext_;

    // Cache de completions para performance
    std::unordered_map<std::string, std::vector<CompletionItem>>
        completionCache_;

#ifdef CLANG_COMPLETION_ENABLED
    // Argumentos do compilador para LibTooling
    std::vector<std::string> compiler_args_;
#endif

    // Helpers internos
    void initializeClang();
    void cleanupClang();
    std::string buildTempFile(const std::string &partialCode) const;

#ifdef CLANG_COMPLETION_ENABLED
    std::vector<CompletionItem>
    getCompletionsWithClang(const std::string &partialCode, int line,
                            int column);
#endif
    std::vector<CompletionItem>
    getCompletionsMock(const std::string &partialCode, int line, int column);

  public:
    ClangCompletion();
    ~ClangCompletion();

    // Não copiável (devido aos recursos clang)
    ClangCompletion(const ClangCompletion &) = delete;
    ClangCompletion &operator=(const ClangCompletion &) = delete;

    /**
     * @brief Atualiza o contexto atual do REPL
     *
     * Deve ser chamado sempre que o contexto muda (nova variável, função, etc.)
     */
    void updateReplContext(const ReplContext &context);

    /**
     * @brief Obtem completions para texto parcial na posição atual
     *
     * @param partialCode Código sendo digitado
     * @param line Linha atual (1-based)
     * @param column Coluna atual (1-based)
     * @return Vetor de possíveis completions ordenados por prioridade
     */
    std::vector<CompletionItem> getCompletions(const std::string &partialCode,
                                               int line, int column);

    /**
     * @brief Obtem documentação para um símbolo específico
     */
    std::string getDocumentation(const std::string &symbol);

    /**
     * @brief Obtem diagnosticos (erros/warnings) para o código atual
     */
    std::vector<std::string> getDiagnostics(const std::string &code);

    /**
     * @brief Verifica se um símbolo existe no contexto atual
     */
    bool symbolExists(const std::string &symbol) const;

    /**
     * @brief Limpa o cache de completions
     */
    void clearCache();
};

} // namespace completion
