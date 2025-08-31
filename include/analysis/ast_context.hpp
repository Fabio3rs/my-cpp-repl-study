#pragma once

#include "../../simdjson.h"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

// forward declarations
struct VarDecl;

namespace analysis {

/**
 * @brief Estrutura para rastreamento de código fonte
 * Esta estrutura mantém informações sobre trechos de código fonte,
 * incluindo o snippet de código, o nome do arquivo, a linha e coluna
 * onde o código começa, e um contador de REPL para identificar
 * a ordem de inserção.
 */
struct CodeTracking {
    std::string codeSnippet;
    std::string filename;
    int64_t line{};
    int64_t column{};

    int64_t replCounter{};

    CodeTracking() = default;
    CodeTracking(std::string file, int64_t ln = {}, int64_t col = {},
                 int64_t repl = {})
        : filename(std::move(file)), line(ln), column(col), replCounter(repl) {}
};

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
     * @return true se o include foi adicionado, false se já existia
     */
    static bool addInclude(const std::string &includePath);

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

    static bool staticSaveHeaderToFile(const std::string &filename);

    static void regenerateOutputHeaderWithSnippets();

  private:
    /**
     * @brief Header de declarações com duração estática
     *
     * CRÍTICO: Esta variável NUNCA deve ser limpa durante a execução do REPL
     * pois é usada para gerar decl_amalgama.hpp com todas as declarações
     * acumuladas ao longo da sessão. Limpar esta variável quebra a
     * funcionalidade básica do REPL.
     * NEW: para cada adição nova no outputHeader_, deve haver um equivalente em
     * codeSnippets_
     */
    static std::string outputHeader_;
    static std::unordered_set<std::string> includedFiles_;
    static std::vector<CodeTracking> codeSnippets_;
    mutable size_t lastHeaderSize_ = 0;

  public:
    /**
     * @brief Retorna referência constante para todos os registros de tracking
     * @return const std::vector<CodeTracking>&
     */
    const std::vector<CodeTracking> &getCodeSnippets() const {
        return codeSnippets_;
    }

    /**
     * @brief Retorna referência mutável para edição interna dos registros
     * @return std::vector<CodeTracking>&
     */
    std::vector<CodeTracking> &getCodeSnippetsMutable() {
        return codeSnippets_;
    }

    /**
     * @brief Limpa todos os registros de tracking
     */
    void clearCodeSnippets();
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
};

} // namespace analysis
