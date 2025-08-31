#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <any>

namespace repl::interfaces {

// Forward declarations
struct CompilerConfig;
struct CompilationResult;
struct ExecutionResult;

// Error types for clear error handling
enum class CompilerError {
    SyntaxError,
    LinkageError,
    FileNotFound,
    PermissionDenied,
    UnsupportedFeature,
    InternalError
};

enum class ExecutionError {
    SymbolNotFound,
    RuntimeException,
    MemoryError,
    InvalidOperation,
    TimeoutExpired
};

enum class ReplError {
    InvalidCommand,
    CompilationFailed,
    ExecutionFailed,
    StateCorrupted,
    ResourceExhausted
};

// Configuration structures
struct CompilerConfig {
    std::string compiler_path = "clang++";
    std::string std_version = "gnu++20";
    std::vector<std::filesystem::path> include_directories;
    std::vector<std::string> link_libraries;
    std::vector<std::string> preprocessor_definitions;
    std::vector<std::string> compiler_flags;
    bool enable_optimization = false;
    bool enable_debug_info = true;
    std::chrono::milliseconds timeout{30000};
};

struct CompilationResult {
    std::filesystem::path library_path;
    std::vector<std::string> exported_symbols;
    std::chrono::milliseconds compilation_time;
    std::string compiler_output;
    int exit_code = 0;
};

struct ExecutionResult {
    std::any return_value;
    std::chrono::microseconds execution_time;
    std::string stdout_output;
    std::string stderr_output;
    bool success = false;
};

// Core interfaces using modern C++ patterns

/**
 * @brief Interface for C++ code compilation
 *
 * Provides clean abstraction over different compiler backends
 * with proper error handling and resource management.
 */
class ICompiler {
public:
    virtual ~ICompiler() = default;

    /**
     * @brief Compile C++ source code to shared library
     * @param source_code The C++ code to compile
     * @param config Compilation configuration
     * @return Compilation result or error
     */
    virtual std::expected<CompilationResult, CompilerError>
    compile(const std::string& source_code, const CompilerConfig& config) = 0;

    /**
     * @brief Check if compiler is available and properly configured
     * @return True if compiler is ready, false otherwise
     */
    virtual bool isAvailable() const = 0;

    /**
     * @brief Get compiler version information
     * @return Version string or error
     */
    virtual std::expected<std::string, CompilerError> getVersion() const = 0;

    /**
     * @brief Validate source code syntax without full compilation
     * @param source_code Code to validate
     * @return True if syntax is valid, error otherwise
     */
    virtual std::expected<bool, CompilerError>
    validateSyntax(const std::string& source_code) const = 0;
};

/**
 * @brief Interface for executing compiled code
 *
 * Handles dynamic library loading, symbol resolution,
 * and safe execution of compiled C++ code.
 */
class IExecutor {
public:
    virtual ~IExecutor() = default;

    /**
     * @brief Load and execute compiled library
     * @param compilation_result Result from compilation
     * @return Execution result or error
     */
    virtual std::expected<ExecutionResult, ExecutionError>
    execute(const CompilationResult& compilation_result) = 0;

    /**
     * @brief Get value of a variable from executed code
     * @param variable_name Name of the variable
     * @return Variable value or error
     */
    virtual std::expected<std::any, ExecutionError>
    getVariable(const std::string& variable_name) = 0;

    /**
     * @brief Set value of a variable in the execution context
     * @param variable_name Name of the variable
     * @param value New value
     * @return Success or error
     */
    virtual std::expected<void, ExecutionError>
    setVariable(const std::string& variable_name, const std::any& value) = 0;

    /**
     * @brief Get list of available variables
     * @return List of variable names
     */
    virtual std::vector<std::string> getAvailableVariables() const = 0;

    /**
     * @brief Clean up execution context and unload libraries
     */
    virtual void cleanup() = 0;
};

/**
 * @brief Interface for managing REPL context and state
 *
 * Thread-safe container for compilation configuration,
 * variable state, and session management.
 */
class IReplContext {
public:
    virtual ~IReplContext() = default;

    // Configuration management
    virtual void setCompilerConfig(const CompilerConfig& config) = 0;
    virtual CompilerConfig getCompilerConfig() const = 0;

    // Include directory management
    virtual void addIncludeDirectory(const std::filesystem::path& path) = 0;
    virtual void removeIncludeDirectory(const std::filesystem::path& path) = 0;
    virtual std::vector<std::filesystem::path> getIncludeDirectories() const = 0;

    // Library management
    virtual void addLinkLibrary(const std::string& library) = 0;
    virtual void removeLinkLibrary(const std::string& library) = 0;
    virtual std::vector<std::string> getLinkLibraries() const = 0;

    // Preprocessor definitions
    virtual void addPreprocessorDefinition(const std::string& definition) = 0;
    virtual void removePreprocessorDefinition(const std::string& definition) = 0;
    virtual std::vector<std::string> getPreprocessorDefinitions() const = 0;

    // Session management
    virtual void reset() = 0;
    virtual std::string getSessionId() const = 0;
};

/**
 * @brief Interface for REPL commands
 *
 * Plugin-style architecture for extending REPL functionality
 * with custom commands.
 */
class ICommand {
public:
    virtual ~ICommand() = default;

    /**
     * @brief Get command name (e.g., "include", "link", "help")
     * @return Command name
     */
    virtual std::string getName() const = 0;

    /**
     * @brief Get command description for help system
     * @return Human-readable description
     */
    virtual std::string getDescription() const = 0;

    /**
     * @brief Get command usage syntax
     * @return Usage string (e.g., "include <header>")
     */
    virtual std::string getUsage() const = 0;

    /**
     * @brief Execute the command
     * @param args Command arguments (excluding command name)
     * @param context REPL context for state access
     * @return Success or error
     */
    virtual std::expected<void, ReplError>
    execute(const std::vector<std::string>& args, IReplContext& context) = 0;
};

/**
 * @brief Main REPL engine interface
 *
 * Orchestrates compilation, execution, and state management
 * for interactive C++ development.
 */
class IReplEngine {
public:
    virtual ~IReplEngine() = default;

    /**
     * @brief Evaluate a C++ statement or expression
     * @param statement C++ code to evaluate
     * @return Execution result or error
     */
    virtual std::expected<ExecutionResult, ReplError>
    evaluateStatement(const std::string& statement) = 0;

    /**
     * @brief Execute a REPL command
     * @param command_line Full command line (e.g., "include <iostream>")
     * @return Success or error
     */
    virtual std::expected<void, ReplError>
    executeCommand(const std::string& command_line) = 0;

    /**
     * @brief Start interactive REPL loop
     * @return Exit code
     */
    virtual int runInteractiveLoop() = 0;

    /**
     * @brief Execute batch of commands from file
     * @param file_path Path to command file
     * @return Success or error
     */
    virtual std::expected<void, ReplError>
    executeFile(const std::filesystem::path& file_path) = 0;

    /**
     * @brief Get REPL context for state access
     * @return Context interface
     */
    virtual IReplContext& getContext() = 0;

    /**
     * @brief Register a custom command
     * @param command Command implementation
     * @return Success or error
     */
    virtual std::expected<void, ReplError>
    registerCommand(std::unique_ptr<ICommand> command) = 0;
};

// Factory functions for creating implementations
std::unique_ptr<ICompiler> createClangCompiler();
std::unique_ptr<IExecutor> createDynamicExecutor();
std::unique_ptr<IReplContext> createReplContext();
std::unique_ptr<IReplEngine> createReplEngine(
    std::unique_ptr<ICompiler> compiler,
    std::unique_ptr<IExecutor> executor,
    std::unique_ptr<IReplContext> context
);

} // namespace repl::interfaces