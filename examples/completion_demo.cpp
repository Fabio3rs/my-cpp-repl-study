#include "completion/readline_integration.hpp"
#include <iostream>

/**
 * @brief Exemplo de como integrar o sistema de autocompletion no REPL
 *
 * Este arquivo demonstra como usar o sistema de completion context-aware
 * sem modificar o código de produção. É apenas para demonstração.
 */

using namespace completion;

void demonstrateCompletion() {
    std::cout << "\n=== Demo: Context-Aware Autocompletion System ===\n\n";

    // 1. Inicializar sistema de completion
    std::cout << "1. Initializing completion system...\n";
    ReadlineCompletionScope completionScope;

    // 2. Simular contexto do REPL
    std::cout << "\n2. Building REPL context...\n";
    ReplContext context = context_builder::buildFromReplState("std::vec");

    // 3. Atualizar contexto no sistema
    std::cout << "\n3. Updating context in completion system...\n";
    ReadlineIntegration::updateContext(context);

    // 4. Demonstrar completions
    std::cout << "\n4. Getting completions for 'std::vec'...\n";
    auto *clangCompletion = ReadlineIntegration::getClangCompletion();
    if (clangCompletion) {
        auto completions = clangCompletion->getCompletions("std::vec", 1, 8);

        std::cout << "\nFound completions:\n";
        for (const auto &completion : completions) {
            std::cout << "  - " << completion.text << " (" << completion.display
                      << ")" << " [Priority: " << completion.priority << "]\n";
            if (!completion.documentation.empty()) {
                std::cout << "    Doc: " << completion.documentation << "\n";
            }
        }
    }

    // 5. Demonstrar documentação
    std::cout << "\n5. Getting documentation for 'vector'...\n";
    if (clangCompletion) {
        auto doc = clangCompletion->getDocumentation("vector");
        std::cout << "Documentation: " << doc << "\n";
    }

    // 6. Demonstrar diagnósticos
    std::cout << "\n6. Getting diagnostics for sample code...\n";
    if (clangCompletion) {
        std::string sampleCode = "cout << \"hello\""; // Missing iostream
        auto diagnostics = clangCompletion->getDiagnostics(sampleCode);

        std::cout << "Diagnostics for '" << sampleCode << "':\n";
        for (const auto &diagnostic : diagnostics) {
            std::cout << "  - " << diagnostic << "\n";
        }
    }

    std::cout << "\n=== Demo Complete ===\n";
}

int main() {
    std::cout << "C++ REPL - Context-Aware Autocompletion Demo\n";
    std::cout << "============================================\n";

    try {
        demonstrateCompletion();

        std::cout << "\n🎯 Next Steps for Real Implementation:\n";
        std::cout << "  1. Install libclang-dev package\n";
        std::cout << "  2. Add CMake configuration for libclang\n";
        std::cout
            << "  3. Replace mock implementations with real libclang calls\n";
        std::cout << "  4. Integrate with actual REPL state "
                     "(replState.varsNames, etc.)\n";
        std::cout << "  5. Add to main REPL initialization\n";

    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}

/**
 * @brief Como integrar no REPL principal (para referência futura):
 *
 * 1. Em repl.cpp, adicionar no início da função principal:
 *
 *    #include "completion/readline_integration.hpp"
 *
 *    // No início do main ou da função de REPL
 *    completion::ReadlineCompletionScope completionScope;
 *
 * 2. Atualizar contexto quando necessário:
 *
 *    // Após adicionar variável/função
 *    auto context =
 * completion::context_builder::buildFromReplState(currentInput);
 *    completion::ReadlineIntegration::updateContext(context);
 *
 * 3. O autocompletion funcionará automaticamente com Tab
 *
 * 4. Para completions mais precisos, conectar com replState real:
 *    - extractVariableDeclarations() → usar replState.varsNames
 *    - extractFunctionDeclarations() → usar funções definidas
 *    - extractIncludes() → usar includes atuais
 */
