// Examples/verbosity_demo.cpp - Demonstrating verbosity control
#include "../include/completion/clang_completion.hpp"
#include "../include/completion/readline_integration.hpp"
#include "../repl.hpp"
#include <iostream>

using namespace completion;

int main() {
    std::cout << "=== ClangCompletion Verbosity Demo ===\n\n";

    // Create a BuildSettings structure
    BuildSettings settings;
    settings.includeDirectories.insert("/usr/include");
    settings.includeDirectories.insert("/usr/local/include");
    settings.preprocessorDefinitions.insert("DEBUG=1");
    settings.linkLibraries.insert("pthread");

    std::cout << "1. Testing with different verbosity levels:\n\n";

    // Test verbosity level 0 (silent)
    std::cout << "--- Verbosity Level 0 (Silent) ---\n";
    {
        ClangCompletion completion(0); // Silent mode
        completion.initialize(settings);
        std::cout << "Verbosity level: " << completion.getVerbosity() << "\n";
        std::cout << "Note: No debug output should appear above\n\n";
    }

    // Test verbosity level 1 (normal)
    std::cout << "--- Verbosity Level 1 (Normal) ---\n";
    {
        ClangCompletion completion(1); // Normal debug output
        completion.initialize(settings);
        std::cout << "Verbosity level: " << completion.getVerbosity() << "\n\n";
    }

    // Test verbosity level 2 (verbose)
    std::cout << "--- Verbosity Level 2 (Verbose) ---\n";
    {
        ClangCompletion completion(2); // Verbose mode
        completion.initialize(settings);
        std::cout << "Verbosity level: " << completion.getVerbosity() << "\n\n";
    }

    // Test changing verbosity at runtime
    std::cout << "--- Runtime Verbosity Changes ---\n";
    {
        ClangCompletion completion(0); // Start silent
        std::cout << "Initial verbosity: " << completion.getVerbosity() << "\n";

        completion.setVerbosity(2); // Change to verbose
        std::cout << "Changed verbosity to: " << completion.getVerbosity()
                  << "\n";
        completion.initialize(settings); // This should show debug output
    }

    std::cout << "\n=== ReadlineIntegration with BuildSettings ===\n";

    // Test ReadlineIntegration with BuildSettings
    {
        ReadlineIntegration integration;
        integration.initialize(settings);
        std::cout << "ReadlineIntegration initialized with BuildSettings\n";
    }

    std::cout << "\n=== Demo Complete ===\n";
    return 0;
}
