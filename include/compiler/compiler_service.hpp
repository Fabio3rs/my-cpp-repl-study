#pragma once

#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// Forward declarations
struct VarDecl;
struct BuildSettings;
struct ReplState;

namespace analysis {
class AstContext;
class ClangAstAnalyzerAdapter;
} // namespace analysis

namespace compiler {

/**
 * @brief Errors that can occur during compilation operations
 */
enum class CompilerError {
    Success = 0,
    BuildFailed,
    AstAnalysisFailed,
    LinkingFailed,
    PrecompiledHeaderFailed,
    FileWriteFailed,
    SystemCommandFailed
};

/**
 * @brief Result template for operations that can fail
 */
template <typename T> struct CompilerResult {
    T value{};
    CompilerError error = CompilerError::Success;

    bool success() const { return error == CompilerError::Success; }
    explicit operator bool() const { return success(); }

    const T &operator*() const { return value; }
    T &operator*() { return value; }
    const T *operator->() const { return &value; }
    T *operator->() { return &value; }
};

// Specialization for void
template <> struct CompilerResult<void> {
    CompilerError error = CompilerError::Success;

    bool success() const { return error == CompilerError::Success; }
    explicit operator bool() const { return success(); }
};

/**
 * @brief Result of compilation operations containing variables and return code
 */
struct CompilationResult {
    std::vector<VarDecl> variables;
    int returnCode = 0;
    bool success() const { return returnCode == 0; }
};

/**
 * @brief Service responsible for all compilation operations in the REPL
 *
 * This class encapsulates compilation logic that was previously scattered
 * throughout repl.cpp. It provides a clean interface for building libraries,
 * analyzing AST, and linking objects while minimizing state dependencies.
 *
 * Design Principles:
 * - Minimal state: Only holds references to external state
 * - Thread-safe: All operations are stateless or properly synchronized
 * - Error handling: Uses std::expected for consistent error propagation
 * - Dependency injection: Takes dependencies as constructor parameters
 */
class CompilerService {
  public:
    /**
     * @brief Callback function type for merging variables into global state
     * This allows the service to be stateless while still updating global vars
     */
    using VarMergeCallback = std::function<void(const std::vector<VarDecl> &)>;

  private:
    const BuildSettings *buildSettings_;
    std::shared_ptr<analysis::AstContext> astContext_;
    VarMergeCallback varMergeCallback_;

    // Configuration for parallel processing
    mutable size_t maxThreads_ =
        0; // 0 = auto-detect based on hardware_concurrency

  public:
    /**
     * @brief Constructor with dependency injection
     * @param buildSettings Reference to global build configuration (non-owning)
     * @param astContext Shared context for AST operations
     * @param varMergeCallback Function to merge variables into global state
     */
    explicit CompilerService(
        const BuildSettings *buildSettings,
        std::shared_ptr<analysis::AstContext> astContext = nullptr,
        VarMergeCallback varMergeCallback = nullptr);

    // === Core Compilation Operations ===

    /**
     * @brief Build a shared library from source without AST analysis
     *
     * Equivalent to the original onlyBuildLib() function.
     *
     * @param compiler Compiler command (e.g., "clang++")
     * @param name Library name (without extension)
     * @param ext Source file extension (default: ".cpp")
     * @param std C++ standard (default: "gnu++20")
     * @return CompilerResult<int> - System return code or error
     */
    CompilerResult<int>
    buildLibraryOnly(const std::string &compiler, const std::string &name,
                     const std::string &ext = ".cpp",
                     const std::string &std = "gnu++20",
                     std::string_view extra_args = {}) const;

    /**
     * @brief Build library with full AST analysis and variable extraction
     *
     * Equivalent to the original buildLibAndDumpAST() function.
     * This is the most complete build operation.
     *
     * @param compiler Compiler command
     * @param name Library name
     * @param ext Source file extension
     * @param std C++ standard
     * @return CompilerResult<std::vector<VarDecl>> - Extracted variables or
     * error
     */
    CompilerResult<std::vector<VarDecl>>
    buildLibraryWithAST(const std::string &compiler, const std::string &name,
                        const std::string &ext = ".cpp",
                        const std::string &std = "gnu++20") const;

    /**
     * @brief Build precompiled header for faster compilation
     *
     * Equivalent to the original build_precompiledheader() function.
     *
     * @param compiler Compiler command (default: "clang++")
     * @param context Optional AST context for includes (uses member if null)
     * @return CompilerResult<void> - Success or error
     */
    CompilerResult<void> buildPrecompiledHeader(
        const std::string &compiler = "clang++",
        std::shared_ptr<analysis::AstContext> context = nullptr) const;

    /**
     * @brief Link multiple object files into a shared library
     *
     * Equivalent to the original linkAllObjects() function.
     *
     * @param objects List of object file paths
     * @param libname Output library name
     * @return CompilerResult<int> - System return code or error
     */
    CompilerResult<int> linkObjects(const std::vector<std::string> &objects,
                                    const std::string &libname) const;

    /**
     * @brief Build multiple sources into library with parallel AST analysis
     *
     * Equivalent to the original buildLibAndDumpASTWithoutPrint() function.
     * This is the most complex operation with parallel processing.
     *
     * @param compiler Compiler command
     * @param libname Output library name
     * @param sources List of source file paths
     * @param std C++ standard
     * @return CompilerResult<CompilationResult> - Variables and status or error
     */
    CompilerResult<CompilationResult> buildMultipleSourcesWithAST(
        const std::string &compiler, const std::string &libname,
        const std::vector<std::string> &sources, const std::string &std) const;

    /**
     * @brief Analyze custom compilation commands with parallel processing
     *
     * Equivalent to the original analyzeCustomCommands() function.
     * Processes multiple compilation commands in parallel and extracts AST
     * data.
     *
     * @param commands List of compilation commands to analyze
     * @return CompilerResult<std::vector<std::string>> - List of variables
     * found or error
     */
    CompilerResult<std::vector<std::string>>
    analyzeCustomCommands(const std::vector<std::string> &commands) const;

    // === Helper Operations ===

    /**
     * @brief Get the current AST context
     * @return Shared pointer to AST context (may be null)
     */
    std::shared_ptr<analysis::AstContext> getAstContext() const {
        return astContext_;
    }

    /**
     * @brief Set a new AST context
     * @param context New context to use
     */
    void setAstContext(std::shared_ptr<analysis::AstContext> context) {
        astContext_ = context;
    }

    // === Threading Configuration ===

    /**
     * @brief Set maximum number of threads for parallel operations
     * @param maxThreads Maximum threads (0 = auto-detect from
     * hardware_concurrency)
     */
    void setMaxThreads(size_t maxThreads) const { maxThreads_ = maxThreads; }

    /**
     * @brief Get effective number of threads for parallel operations
     * @return Number of threads that will be used
     */
    size_t getEffectiveThreadCount() const {
        if (maxThreads_ == 0) {
            auto hwThreads = std::thread::hardware_concurrency();
            return hwThreads > 0 ? hwThreads : 4; // fallback to 4 threads
        }
        return maxThreads_;
    }

    static bool checkIncludeExists(const BuildSettings &settings,
                                   const std::string &includePath);

  private:
    // === Internal Helper Methods ===

    /**
     * @brief Generate include precompiled header flag based on file extension
     * @param ext File extension
     * @return Include flag string (empty for .c files)
     */
    std::string getPrecompiledHeaderFlag(const std::string &ext) const;

    /**
     * @brief Build command string for compilation
     * @param compiler Compiler name
     * @param std C++ standard
     * @param flags Additional compilation flags
     * @param inputFile Input file path
     * @param outputFile Output file path
     * @return Complete command string
     */
    std::string buildCompileCommand(const std::string &compiler,
                                    const std::string &std,
                                    const std::string &flags,
                                    const std::string &inputFile,
                                    const std::string &outputFile) const;

    /**
     * @brief Execute system command and return result
     * @param command Command to execute
     * @return CompilerResult<int> - Return code or error
     */
    CompilerResult<int> executeCommand(const std::string &command) const;

    /**
     * @brief Concatenate filenames with spaces for linking
     * @param names List of filenames
     * @return Space-separated string
     */
    std::string concatenateNames(const std::vector<std::string> &names) const;

    /**
     * @brief Get current build settings strings (thread-safe)
     */
    std::string getLinkLibrariesStr() const;
    std::string getIncludeDirectoriesStr() const;
    std::string getPreprocessorDefinitionsStr() const;

    /**
     * @brief Read contents of a log file
     * @param logPath Path to the log file
     * @return Log file contents or empty string if file doesn't exist
     */
    std::string readLogFile(const std::string &logPath) const;

    /**
     * @brief Print compilation error with context
     * @param logPath Path to the error log file
     * @param context Description of what was being compiled
     */
    void printCompilationError(const std::string &logPath,
                               const std::string &context) const;

    /**
     * @brief Check if terminal supports colors
     * @return true if colors should be used
     */
    bool shouldUseColors() const;

    /**
     * @brief Get ANSI color code for specified color
     * @param color Color name (red, yellow, green, blue, reset)
     * @return ANSI color code or empty string if colors disabled
     */
    std::string getColorCode(const std::string &color) const;

    /**
     * @brief Format error line with appropriate colors
     * @param line Log line to format
     * @return Formatted line with colors
     */
    std::string formatErrorLine(const std::string &line) const;
};

} // namespace compiler
