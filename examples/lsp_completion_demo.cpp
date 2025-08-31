#include "../repl.hpp"
#include "lsp_readline_integration.hpp"
#include <iostream>
#include <string>
#include <vector>

/**
 * @brief Demonstração da integração LSP/clangd com readline
 *
 * Este demo mostra como usar o novo sistema de completion baseado em clangd
 * LSP, que oferece precisão e performance superiores ao sistema baseado em
 * libclang.
 */

using namespace completion;

int main() {
    std::cout << "C++ REPL - LSP/clangd Integration Demo\n";
    std::cout << "=====================================\n\n";

    try {
        // Configurar opções do clangd
        LspReadlineIntegration::Config config;
        config.verboseLogging = true;
        config.enableDiagnostics = true;
        config.maxCompletions = 50;
        config.preambleTimeoutMs = 3000;
        config.completionTimeoutMs = 1500;

        // Configurações específicas do clangd
        config.clangdOpts.backgroundIndex = true;
        config.clangdOpts.pchInMemory = true;
        config.clangdOpts.extraClangdArgs = {"--log=verbose", "--pretty",
                                             "--completion-style=detailed"};
        config.clangdOpts.extraFallbackFlags = {"-std=c++20", "-Wall",
                                                "-Wextra"};

        std::cout << "=== Demo: LSP-based Context-Aware Autocompletion ===\n\n";

        // Criar scope RAII para gerenciar lifetime
        std::cout << "1. Creating LSP integration scope...\n";
        LspReadlineIntegration::Scope lspScope(config);

        // Configurar compilador e build settings
        std::cout << "2. Configuring compiler and build settings...\n";

        CompilerCodeCfg codeCfg;
        codeCfg.std = "c++20";
        codeCfg.compiler = "clang++";
        codeCfg.analyze = true;
        codeCfg.addIncludes = true;

        BuildSettings buildSettings;

        // Inicializar integração
        auto *integration = lspScope.getInstance();
        if (!integration->initialize(codeCfg, buildSettings)) {
            std::cerr << "Failed to initialize LSP integration\n";
            return 1;
        }

        std::cout << "3. Building comprehensive REPL context...\n";

        // Simular estado do REPL com contexto rico
        std::vector<std::string> includes = {
            "<iostream>",  "<vector>", "<string>", "<memory>",
            "<algorithm>", "<chrono>", "<thread>"};

        std::vector<std::string> variables = {
            "int counter = 42",
            "std::string message = \"Hello, LSP!\"",
            "std::vector<int> numbers = {1, 2, 3, 4, 5}",
            "std::unique_ptr<int> smartPtr = std::make_unique<int>(100)",
            "auto lambda = [](int x) { return x * 2; }",
            "std::chrono::high_resolution_clock::time_point start = "
            "std::chrono::high_resolution_clock::now()"};

        std::vector<std::string> functions = {
            "void printMessage(const std::string& msg)",
            "template<typename T> T multiply(T a, T b)",
            "std::vector<int> processNumbers(const std::vector<int>& input)",
            "class ReplClass { public: void doSomething(); private: int "
            "value_; }"};

        // Construir contexto usando o builder
        auto context = lsp_context_builder::buildFromReplState(
            "std::vec", includes, variables, functions);

        std::cout << "4. Updating context in LSP integration...\n";
        lspScope.updateReplContext(context);

        // Aguardar um pouco para o clangd processar
        std::cout << "5. Waiting for clangd to process context...\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));

        std::cout << "6. Testing various completion scenarios...\n\n";

        // Teste 1: Completions para std::
        std::cout << "=== Test 1: std:: completions ===\n";
        auto stdCompletions = integration->getCompletions("std::", 1, 5);
        std::cout << "Found " << stdCompletions.size()
                  << " completions for 'std::'\n";

        // Mostrar primeiros 10
        int count = 0;
        for (const auto &completion : stdCompletions) {
            if (++count > 10)
                break;
            std::cout << "  - " << completion.text;
            if (!completion.signature.empty()) {
                std::cout << " [" << completion.signature << "]";
            }
            std::cout << "\n";
        }
        std::cout << "\n";

        // Teste 2: Completions para std::vec
        std::cout << "=== Test 2: std::vec completions ===\n";
        auto vecCompletions = integration->getCompletions("std::vec", 1, 8);
        std::cout << "Found " << vecCompletions.size()
                  << " completions for 'std::vec'\n";
        for (const auto &completion : vecCompletions) {
            std::cout << "  - " << completion.text;
            if (!completion.signature.empty()) {
                std::cout << " [" << completion.signature << "]";
            }
            std::cout << "\n";
        }
        std::cout << "\n";

        // Teste 3: Completions para variáveis do contexto
        std::cout << "=== Test 3: Context variable completions ===\n";
        auto contextCompletions = integration->getCompletions("coun", 1, 4);
        std::cout << "Found " << contextCompletions.size()
                  << " completions for 'coun'\n";
        for (const auto &completion : contextCompletions) {
            std::cout << "  - " << completion.text;
            if (!completion.signature.empty()) {
                std::cout << " [" << completion.signature << "]";
            }
            std::cout << "\n";
        }
        std::cout << "\n";

        // Teste 4: Completions para métodos de objetos do contexto
        std::cout << "=== Test 4: Object method completions ===\n";
        auto methodCompletions = integration->getCompletions("numbers.", 1, 8);
        std::cout << "Found " << methodCompletions.size()
                  << " completions for 'numbers.'\n";
        count = 0;
        for (const auto &completion : methodCompletions) {
            if (++count > 15)
                break;
            std::cout << "  - " << completion.text;
            if (!completion.signature.empty()) {
                std::cout << " [" << completion.signature << "]";
            }
            std::cout << "\n";
        }
        std::cout << "\n";

        // Teste 5: Diagnostics para código com erro
        std::cout << "=== Test 5: Diagnostics for erroneous code ===\n";
        std::string errorCode = "int x = undefined_variable + 42;";
        auto diagnostics = integration->getDiagnostics(errorCode);
        std::cout << "Found " << diagnostics.size()
                  << " diagnostics for erroneous code\n";
        for (const auto &diag : diagnostics) {
            std::cout << "  - Line " << (diag.range.start.line + 1) << ", Col "
                      << (diag.range.start.character + 1) << ": "
                      << diag.message << "\n";
        }
        std::cout << "\n";

        std::cout << "=== Demo Complete ===\n";
        std::cout << "LSP/clangd integration provides:\n";
        std::cout << "✅ Superior completion accuracy\n";
        std::cout << "✅ Real-time diagnostics\n";
        std::cout << "✅ Context-aware suggestions\n";
        std::cout << "✅ Performance with persistent indexing\n";
        std::cout << "✅ Modern C++20/23 support\n\n";

        std::cout << "🎯 Next Steps:\n";
        std::cout << "  1. Integrate with main REPL loop\n";
        std::cout << "  2. Add hover information support\n";
        std::cout << "  3. Implement go-to-definition\n";
        std::cout << "  4. Add signature help\n";
        std::cout << "  5. Real-time error highlighting\n";

    } catch (const std::exception &e) {
        std::cerr << "Exception: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
