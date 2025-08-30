/**
 * @file integration_example.cpp
 * @brief Exemplo de como integrar ClangCompletion com BuildSettings no REPL
 */

#include "completion/clang_completion.hpp"
#include "completion/readline_integration.hpp"
#include "repl.hpp"
#include <iostream>

/**
 * Exemplo de como usar ClangCompletion com BuildSettings
 */
void demonstrateClangCompletionIntegration() {
    std::cout
        << "=== ClangCompletion com BuildSettings Integration Demo ===\n\n";

    // 1. Simular configurações do REPL (normalmente vem de buildSettings
    // global)
    BuildSettings settings;

    // Adicionar diretórios de include customizados
    settings.includeDirectories.insert("/usr/include/boost");
    settings.includeDirectories.insert("/opt/local/include");
    settings.includeDirectories.insert("./custom_headers");

    // Adicionar definições de preprocessor
    settings.preprocessorDefinitions.insert("DEBUG_MODE=1");
    settings.preprocessorDefinitions.insert("CUSTOM_FEATURE");
    settings.preprocessorDefinitions.insert("VERSION_MAJOR=2");

    // Adicionar bibliotecas de link
    settings.linkLibraries.insert("pthread");
    settings.linkLibraries.insert("boost_system");
    settings.linkLibraries.insert("custom_lib");

    std::cout << "1. Configurações BuildSettings criadas:\n";
    std::cout << "   - Include directories: "
              << settings.includeDirectories.size() << "\n";
    std::cout << "   - Preprocessor definitions: "
              << settings.preprocessorDefinitions.size() << "\n";
    std::cout << "   - Link libraries: " << settings.linkLibraries.size()
              << "\n\n";

    // 2. Inicializar sistema de completion usando ReadlineIntegration
    std::cout << "2. Inicializando sistema de completion...\n";
    completion::ReadlineIntegration::initialize(settings);

    // 3. Criar contexto REPL de exemplo
    completion::ReplContext context;
    context.currentIncludes =
        "#include <iostream>\n#include <vector>\n#include "
        "<boost/algorithm/string.hpp>\n";
    context.variableDeclarations = "int counter = 0;\nstd::vector<std::string> "
                                   "items;\nboost::algorithm::split_iterator<"
                                   "std::string::iterator> iter;\n";
    context.functionDeclarations =
        "void processItems(const std::vector<std::string>& items);\nint "
        "calculateTotal(int base);\n";
    context.activeCode =
        "std::cout << \"Processing \" << items.size() << \" items\\n\";\n";

    std::cout << "3. Contexto REPL criado e configurado\n\n";

    // 4. Atualizar contexto no sistema de completion
    completion::ReadlineIntegration::updateContext(context);

    // 5. Obter completions de exemplo
    auto *clangCompletion =
        completion::ReadlineIntegration::getClangCompletion();
    if (clangCompletion) {
        std::cout << "4. Testando completions:\n\n";

        // Teste 1: Completion para std::
        auto completions1 = clangCompletion->getCompletions("std::", 1, 5);
        std::cout << "   Completions para 'std::': " << completions1.size()
                  << " encontrados\n";

        // Teste 2: Completion para boost::
        auto completions2 = clangCompletion->getCompletions("boost::", 1, 7);
        std::cout << "   Completions para 'boost::': " << completions2.size()
                  << " encontrados\n";

        // Teste 3: Completion para variáveis locais
        auto completions3 = clangCompletion->getCompletions("coun", 1, 4);
        std::cout << "   Completions para 'coun': " << completions3.size()
                  << " encontrados\n";

        // Teste 4: Verificar se símbolos existem
        bool exists1 = clangCompletion->symbolExists("counter");
        bool exists2 = clangCompletion->symbolExists("boost");
        bool exists3 = clangCompletion->symbolExists("nonexistent");

        std::cout << "   Symbol 'counter' exists: " << (exists1 ? "yes" : "no")
                  << "\n";
        std::cout << "   Symbol 'boost' exists: " << (exists2 ? "yes" : "no")
                  << "\n";
        std::cout << "   Symbol 'nonexistent' exists: "
                  << (exists3 ? "yes" : "no") << "\n";

        // Teste 5: Obter documentação
        auto doc = clangCompletion->getDocumentation("vector");
        std::cout << "   Documentation for 'vector':\n     " << doc << "\n\n";

        // Teste 6: Diagnósticos de código
        auto diagnostics = clangCompletion->getDiagnostics("cout << counter");
        std::cout << "   Diagnostics for 'cout << counter': "
                  << diagnostics.size() << " issues\n";
        for (const auto &diag : diagnostics) {
            std::cout << "     - " << diag << "\n";
        }
    } else {
        std::cout << "4. ERROR: ClangCompletion não inicializado!\n";
    }

    // 6. Cleanup
    std::cout << "\n5. Fazendo cleanup...\n";
    completion::ReadlineIntegration::cleanup();

    std::cout << "\n=== Demo Completo ===\n";
}

/**
 * Exemplo de integração direta (sem ReadlineIntegration)
 */
void demonstrateDirectClangCompletionUsage() {
    std::cout << "\n=== Uso Direto do ClangCompletion ===\n\n";

    // Criar configurações
    BuildSettings settings;
    settings.includeDirectories.insert("/usr/include/eigen3");
    settings.preprocessorDefinitions.insert("EIGEN_USE_THREADS");
    settings.linkLibraries.insert("eigen3");

    // Criar instância diretamente
    completion::ClangCompletion clangCompletion;
    clangCompletion.initialize(settings);

    // Usar diretamente
    auto completions = clangCompletion.getCompletions("Eigen::", 1, 7);
    std::cout << "Completions para 'Eigen::': " << completions.size()
              << " encontrados\n\n";
}

int main() {
    std::cout << "C++ REPL - ClangCompletion Integration Examples\n";
    std::cout << "===============================================\n\n";

    try {
        demonstrateClangCompletionIntegration();
        demonstrateDirectClangCompletionUsage();
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
