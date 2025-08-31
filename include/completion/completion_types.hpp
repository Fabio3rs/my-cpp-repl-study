#pragma once

#include <string>
#include <vector>

// Forward declarations para estruturas definidas em repl.hpp
struct CompilerCodeCfg;
struct BuildSettings;

namespace completion {

/**
 * @brief Item de completion retornado pelos provedores
 */
struct CompletionItem {
    std::string text;          // Texto a ser inserido
    std::string display;       // Texto exibido na lista
    std::string documentation; // Documentação do item
    std::string returnType;    // Tipo de retorno (para funções)
    std::string signature;     // Assinatura da função/método
    int priority{};            // Prioridade para ordenação

    enum class Kind {
        Variable,
        Function,
        Class,
        Struct,
        Enum,
        Keyword,
        Include,
        Macro,
        Method,
        Field,
        Constructor,
        Property,
        Constant,
        Interface,
        Module,
        Unit,
        Value,
        Snippet,
        Color,
        File,
        Reference
    } kind{Kind::Variable};
};

/**
 * @brief Contexto do REPL para completion
 */
struct ReplContext {
    std::string currentIncludes;      // Includes acumulados
    std::string variableDeclarations; // Declarações de variáveis
    std::string functionDeclarations; // Declarações de funções
    std::string typeDefinitions;      // Definições de tipos
    std::string activeCode;           // Código sendo editado
    int line = 1;                     // Linha atual do cursor
    int column = 1;                   // Coluna atual do cursor
};

/**
 * @brief Informação de diagnóstico (erro/warning/hint)
 */
struct DiagnosticInfo {
    enum class Severity { Error = 1, Warning = 2, Information = 3, Hint = 4 };

    struct Range {
        struct Position {
            int line;
            int character;
        } start, end;
    };

    Range range;         // Posição do diagnóstico
    Severity severity;   // Severidade do problema
    std::string code;    // Código do diagnóstico (e.g., "unused-variable")
    std::string message; // Mensagem de erro/warning
    std::string source;  // Fonte (e.g., "clangd", "gcc")

    // Construtor de conveniência
    DiagnosticInfo(int startLine, int startChar, int endLine, int endChar,
                   Severity sev, const std::string &msg,
                   const std::string &src = "")
        : range{{startLine, startChar}, {endLine, endChar}}, severity(sev),
          message(msg), source(src) {}

    DiagnosticInfo() = default;
};

} // namespace completion
