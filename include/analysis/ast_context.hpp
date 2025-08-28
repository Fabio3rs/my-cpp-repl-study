#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

// forward declarations
struct VarDecl;

namespace analysis {

/**
 * @brief Contexto para análise AST que encapsula o estado compartilhado
 *
 * Esta classe substitui as variáveis globais outputHeader e includedFiles,
 * fornecendo um contexto thread-safe para análise AST.
 */
class AstContext {
  public:
    AstContext() = default;
    ~AstContext() = default;

    // Não copiável, mas movível
    AstContext(const AstContext &) = delete;
    AstContext &operator=(const AstContext &) = delete;
    AstContext(AstContext &&) = default;
    AstContext &operator=(AstContext &&) = default;

    /**
     * @brief Adiciona um include ao header de saída
     * @param includePath Caminho do arquivo de include
     */
    void addInclude(const std::string &includePath);

    /**
     * @brief Adiciona uma declaração extern ao header de saída
     * @param declaration Declaração a ser adicionada
     */
    void addDeclaration(const std::string &declaration);

    /**
     * @brief Adiciona uma diretiva #line ao header de saída
     * @param line Número da linha
     * @param file Arquivo de origem
     */
    void addLineDirective(int64_t line, const std::filesystem::path &file);

    /**
     * @brief Verifica se um arquivo já foi incluído
     * @param filePath Caminho do arquivo
     * @return true se já foi incluído, false caso contrário
     */
    bool isFileIncluded(const std::string &filePath) const;

    /**
     * @brief Marca um arquivo como incluído
     * @param filePath Caminho do arquivo
     */
    void markFileIncluded(const std::string &filePath);

    /**
     * @brief Obtém o header de saída completo
     * @return String contendo todas as declarações
     */
    const std::string &getOutputHeader() const { return outputHeader_; }

    /**
     * @brief Verifica se o header foi modificado desde a última verificação
     * @return true se foi modificado, false caso contrário
     */
    bool hasHeaderChanged() const;

    /**
     * @brief Limpa todo o contexto
     */
    void clear();

    /**
     * @brief Salva o header em um arquivo
     * @param filename Nome do arquivo de destino
     * @return true se salvou com sucesso, false caso contrário
     */
    bool saveHeaderToFile(const std::string &filename) const;

  private:
    /**
     * @brief Header de declarações com duração estática
     *
     * CRÍTICO: Esta variável NUNCA deve ser limpa durante a execução do REPL
     * pois é usada para gerar decl_amalgama.hpp com todas as declarações
     * acumuladas ao longo da sessão. Limpar esta variável quebra a
     * funcionalidade básica do REPL.
     */
    static std::string outputHeader_;
    std::unordered_set<std::string> includedFiles_;
    mutable size_t lastHeaderSize_ = 0;
};

/**
 * @brief Analisador AST que usa AstContext
 */
class ContextualAstAnalyzer {
  public:
    explicit ContextualAstAnalyzer(std::shared_ptr<AstContext> context);

    /**
     * @brief Analisa a AST interna de forma recursiva
     * @param source Arquivo de origem
     * @param vars Vector para armazenar as variáveis encontradas
     * @param inner Valor JSON da AST interna (tipo genérico)
     */
    void analyzeInnerAST(std::filesystem::path source,
                         std::vector<VarDecl> &vars, void *inner);

    /**
     * @brief Analisa AST a partir de uma string JSON
     * @param json String JSON contendo a AST
     * @param source Arquivo de origem
     * @param vars Vector para armazenar as variáveis encontradas
     * @return Código de saída (0 para sucesso)
     */
    int analyzeASTFromJsonString(std::string_view json,
                                 const std::string &source,
                                 std::vector<VarDecl> &vars);

    /**
     * @brief Analisa AST a partir de um arquivo JSON
     * @param filename Nome do arquivo JSON
     * @param source Arquivo de origem
     * @param vars Vector para armazenar as variáveis encontradas
     * @return Código de saída (0 para sucesso)
     */
    int analyzeASTFile(const std::string &filename, const std::string &source,
                       std::vector<VarDecl> &vars);

    /**
     * @brief Obtém o contexto AST
     * @return Ponteiro compartilhado para o contexto
     */
    std::shared_ptr<AstContext> getContext() const { return context_; }

  private:
    std::shared_ptr<AstContext> context_;

    /**
     * @brief Extrai e adiciona definição completa de classe/struct ao contexto
     * @param element Elemento JSON da classe/struct
     * @param source Arquivo de origem
     * @param lastfile Último arquivo processado
     * @param lastLine Última linha processada
     */
    void extractCompleteClassDefinition(
        simdjson::simdjson_result<simdjson::ondemand::value> &element,
        const std::filesystem::path &source,
        const std::filesystem::path &lastfile, int64_t lastLine);

    /**
     * @brief Extrai lista de parâmetros de um tipo de função
     * @param functionType String do tipo da função
     * @return Lista de parâmetros
     */
    std::string extractParameterList(const std::string &functionType);

    /**
     * @brief Extrai tipo de retorno de um tipo de função
     * @param functionType String do tipo da função
     * @return Tipo de retorno
     */
    std::string extractReturnType(const std::string &functionType);
};

} // namespace analysis
