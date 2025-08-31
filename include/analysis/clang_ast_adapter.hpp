#pragma once

#include "ast_analyzer.hpp"
#include "ast_context.hpp"
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// forward declaration
struct VarDecl;

namespace analysis {

/**
 * @brief Adapter para o analisador AST do Clang usando contexto
 *
 * Esta classe implementa a interface IAstAnalyzer e fornece uma ponte
 * entre o sistema legacy e o novo sistema contextual. Usa internamente
 * o ContextualAstAnalyzer com um contexto compartilhado.
 */
class ClangAstAnalyzerAdapter : public IAstAnalyzer {
  private:
    std::shared_ptr<AstContext> context_;
    std::unique_ptr<ContextualAstAnalyzer> analyzer_;

  public:
    /**
     * @brief Construtor padrão - cria contexto e analisador automaticamente
     */
    ClangAstAnalyzerAdapter();

    /**
     * @brief Construtor com dependências injetadas
     * @param context Contexto AST compartilhado
     * @param analyzer Analisador contextual
     */
    ClangAstAnalyzerAdapter(std::shared_ptr<AstContext> context,
                            std::unique_ptr<ContextualAstAnalyzer> analyzer);

    // Não copiável, apenas movível
    ClangAstAnalyzerAdapter(const ClangAstAnalyzerAdapter &) = delete;
    ClangAstAnalyzerAdapter(ClangAstAnalyzerAdapter &&) = default;
    ClangAstAnalyzerAdapter &
    operator=(const ClangAstAnalyzerAdapter &) = delete;
    ClangAstAnalyzerAdapter &operator=(ClangAstAnalyzerAdapter &&) = default;

    /**
     * @brief Destrutor virtual
     */
    ~ClangAstAnalyzerAdapter() override = default;

    /**
     * @brief Analisa JSON AST em memória
     * @param json String JSON contendo a AST
     * @param source Arquivo de origem
     * @param vars Vector para armazenar variáveis encontradas
     * @return 0 para sucesso, código de erro caso contrário
     */
    int analyzeJson(std::string_view json, const std::string &source,
                    std::vector<VarDecl> &vars) override;

    /**
     * @brief Analisa arquivo JSON AST no disco
     * @param jsonFilename Nome do arquivo JSON
     * @param source Arquivo de origem
     * @param vars Vector para armazenar variáveis encontradas
     * @return 0 para sucesso, código de erro caso contrário
     */
    int analyzeFile(const std::string &jsonFilename, const std::string &source,
                    std::vector<VarDecl> &vars) override;

    /**
     * @brief Obtém o contexto AST compartilhado
     * @return Ponteiro compartilhado para o contexto
     */
    std::shared_ptr<AstContext> getContext() const override;

    /**
     * @brief Obtém o analisador contextual interno
     * @return Referência para o analisador contextual
     */
    const ContextualAstAnalyzer &getAnalyzer() const;

    /**
     * @brief Cria uma nova instância com contexto compartilhado
     * @param context Contexto a ser compartilhado
     * @return Nova instância do adapter
     */
    static ClangAstAnalyzerAdapter
    createWithSharedContext(std::shared_ptr<AstContext> context);
};

} // namespace analysis
