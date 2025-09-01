# C++ REPL - API Reference

This document provides comprehensive API reference for all public interfaces in the C++ REPL system, extracted directly from the source code.

## Table of Contents

- [Core REPL Interface](#core-repl-interface)
- [Compiler Service API](#compiler-service-api)
- [Execution Engine API](#execution-engine-api)
- [Symbol Resolver API](#symbol-resolver-api)
- [AST Context API](#ast-context-api)
- [Command System API](#command-system-api)
- [Completion System API](#completion-system-api)
- [Utility APIs](#utility-apis)

## Core REPL Interface

### Main Functions (repl.hpp)

```cpp
/**
 * @brief Initialize the REPL system
 * Must be called before any other REPL operations
 */
void initRepl();

/**
 * @brief Start interactive REPL loop
 * Runs until user types 'exit' or EOF
 */
void repl();

/**
 * @brief Execute a single REPL command
 * @param cmd Command string to execute
 * @return true if execution successful, false to exit REPL
 */
bool extExecRepl(std::string_view cmd);

/**
 * @brief Install Ctrl+C signal handler for graceful interruption
 */
void installCtrlCHandler();

/**
 * @brief Initialize desktop notification system
 * @param appName Application name for notifications
 */
void initNotifications(std::string_view appName);

/**
 * @brief Send error notification to desktop
 * @param summary Brief error summary
 * @param msg Detailed error message
 */
void notifyError(std::string_view summary, std::string_view msg);
```

### Data Structures

```cpp
/**
 * @brief Variable declaration information from AST analysis
 */
struct VarDecl {
    std::string name;        ///< Variable name
    std::string mangledName; ///< Mangled symbol name
    std::string type;        ///< Type string
    std::string qualType;    ///< Qualified type string
    std::string kind;        ///< Declaration kind (var, func, etc.)
    std::string file;        ///< Source file
    int line;               ///< Line number
};

/**
 * @brief Result of REPL evaluation
 */
struct EvalResult {
    std::string libpath;           ///< Path to generated library
    std::function<void()> exec;    ///< Execution function
    void* handle{};               ///< Library handle
    bool success{};               ///< Success status
    
    operator bool() const { return success; }
};

/**
 * @brief Compiler configuration for code generation
 */
struct CompilerCodeCfg {
    std::string compiler = "clang++";     ///< Compiler command
    std::string std = "gnu++20";          ///< C++ standard
    std::string extension = "cpp";        ///< File extension
    std::string repl_name;               ///< REPL session name
    std::string libraryName;             ///< Output library name
    std::string wrapperName;             ///< Wrapper library name
    std::vector<std::string> sourcesList; ///< Source file list
    bool lazyEval = false;               ///< Lazy evaluation mode
    bool use_cpp2 = false;               ///< cpp2 syntax mode
    bool fileWrap = true;                ///< File wrapping mode
};
```

## Compiler Service API

### CompilerService Class (compiler/compiler_service.hpp)

```cpp
namespace compiler {

/**
 * @brief Error types for compilation operations
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
template<typename T>
struct CompilerResult {
    T value{};
    CompilerError error = CompilerError::Success;
    
    bool success() const;
    explicit operator bool() const;
    const T& operator*() const;
    T& operator*();
};

/**
 * @brief Service responsible for all compilation operations
 */
class CompilerService {
public:
    using VarMergeCallback = std::function<void(const std::vector<VarDecl>&)>;
    
    /**
     * @brief Constructor with dependency injection
     * @param buildSettings Reference to global build configuration
     * @param astContext Shared context for AST operations
     * @param varMergeCallback Function to merge variables into global state
     */
    explicit CompilerService(
        const BuildSettings* buildSettings,
        std::shared_ptr<analysis::AstContext> astContext = nullptr,
        VarMergeCallback varMergeCallback = nullptr);

    // === Core Compilation Operations ===
    
    /**
     * @brief Build a shared library from source without AST analysis
     * @param compiler Compiler command (e.g., "clang++")
     * @param name Library name (without extension)
     * @param ext Source file extension (default: ".cpp")
     * @param std C++ standard (default: "gnu++20")
     * @param extra_args Additional compiler arguments
     * @return CompilerResult<int> - System return code or error
     */
    CompilerResult<int> buildLibraryOnly(
        const std::string& compiler,
        const std::string& name,
        const std::string& ext = ".cpp",
        const std::string& std = "gnu++20",
        std::string_view extra_args = {}) const;
        
    /**
     * @brief Build library with full AST analysis and variable extraction
     * @param compiler Compiler command
     * @param name Library name
     * @param ext Source file extension
     * @param std C++ standard
     * @return CompilerResult<std::vector<VarDecl>> - Extracted variables or error
     */
    CompilerResult<std::vector<VarDecl>> buildLibraryWithAST(
        const std::string& compiler,
        const std::string& name,
        const std::string& ext = ".cpp", 
        const std::string& std = "gnu++20") const;
        
    /**
     * @brief Build precompiled header for faster compilation
     * @param compiler Compiler command (default: "clang++")
     * @param context Optional AST context for includes
     * @return CompilerResult<void> - Success or error
     */
    CompilerResult<void> buildPrecompiledHeader(
        const std::string& compiler = "clang++",
        std::shared_ptr<analysis::AstContext> context = nullptr) const;
        
    /**
     * @brief Link multiple object files into a shared library
     * @param objects List of object file paths
     * @param libname Output library name
     * @return CompilerResult<int> - System return code or error
     */
    CompilerResult<int> linkObjects(
        const std::vector<std::string>& objects,
        const std::string& libname) const;
        
    /**
     * @brief Build multiple sources with parallel AST analysis
     * @param compiler Compiler command
     * @param libname Output library name  
     * @param sources List of source file paths
     * @param std C++ standard
     * @return CompilerResult<CompilationResult> - Variables and status or error
     */
    CompilerResult<CompilationResult> buildMultipleSourcesWithAST(
        const std::string& compiler,
        const std::string& libname,
        const std::vector<std::string>& sources,
        const std::string& std) const;

    // === Threading Configuration ===
    
    /**
     * @brief Set maximum number of threads for parallel operations
     * @param maxThreads Maximum threads (0 = auto-detect)
     */
    void setMaxThreads(size_t maxThreads) const;
    
    /**
     * @brief Get effective number of threads for parallel operations
     * @return Number of threads that will be used
     */
    size_t getEffectiveThreadCount() const;
};

}
```

## Execution Engine API

### ExecutionEngine Class (execution/execution_engine.hpp)

```cpp
namespace execution {

/**
 * @brief Global execution state required for POSIX dlopen/dlsym operations
 */
struct GlobalExecutionState {
    // Shared state for dynamic execution
    std::string lastLibrary;
    std::unordered_map<std::string, uintptr_t> symbolsToResolve;
    std::unordered_map<std::string, wrapperFn> fnNames;
    
    // Global configuration for symbol resolution via trampolines
    SymbolResolver::WrapperConfig wrapperConfig;
    
    // Global counters
    int64_t replCounter = 0;
    int ctrlcounter = 0;
    
    // Thread safety for concurrent access
    mutable std::shared_mutex stateMutex;
    
    // Thread-safe helper methods
    void setLastLibrary(const std::string& library);
    std::string getLastLibrary() const;
    void clearSymbolsToResolve();
    void addSymbolToResolve(const std::string& symbol, uintptr_t address);
    bool hasFnName(const std::string& mangledName) const;
    wrapperFn& getFnName(const std::string& mangledName);
    void setFnName(const std::string& mangledName, const wrapperFn& fn);
};

/**
 * @brief Access to global execution state singleton
 * @return Reference to the global state (thread-safe)
 */
GlobalExecutionState& getGlobalExecutionState();

/**
 * @brief Prepare wrapper and load code library
 * @param cfg Compiler configuration
 * @param vars Variable declarations to process
 * @return EvalResult with execution information
 */
auto prepareWrapperAndLoadCodeLib(const CompilerCodeCfg& cfg, 
                                 std::vector<VarDecl>&& vars) -> EvalResult;

/**
 * @brief Load prebuilt library with symbol resolution
 * @param path Path to prebuilt library
 * @return true if loaded successfully
 */
bool loadPrebuilt(const std::string& path);

}
```

## Symbol Resolver API

### SymbolResolver Class (execution/symbol_resolver.hpp)

```cpp
namespace execution {

/**
 * @brief Dynamic symbol resolution system using trampolines
 */
class SymbolResolver {
public:
    /**
     * @brief Information for a function wrapper
     */
    struct WrapperInfo {
        void* fnptr;        ///< Function pointer
        void** wrap_ptrfn;  ///< Pointer to wrapper function pointer
    };
    
    /**
     * @brief Configuration for wrapper generation
     */
    struct WrapperConfig {
        std::string libraryPath;
        std::string extraArgs{"-nostdlib"};
        std::unordered_map<std::string, uintptr_t> symbolOffsets;
        std::unordered_map<std::string, WrapperInfo> functionWrappers;

        WrapperConfig() = default;
        WrapperConfig(const WrapperConfig &) = delete;
        WrapperConfig &operator=(const WrapperConfig &) = delete;
        WrapperConfig(WrapperConfig &&) = default;
        WrapperConfig &operator=(WrapperConfig &&) = default;
    };

    
    /**
     * @brief Generate C++ code for a function trampoline
     * @param fnvars Function declaration to generate wrapper for
     * @return String containing C++ trampoline code
     */
    static std::string generateFunctionWrapper(const VarDecl& fnvars);
    
    /**
     * @brief Prepare wrappers for a set of functions
     * @param name Base name for wrapper file
     * @param vars List of variable/function declarations
     * @param config Wrapper configuration (will be filled)
     * @param existingFunctions Functions already processed
     * @return Map of mangled names to function names
     */
    static std::unordered_map<std::string, std::string> prepareFunctionWrapper(
        const std::string& name,
        const std::vector<VarDecl>& vars,
        WrapperConfig& config,
        const std::unordered_set<std::string>& existingFunctions);
        
    /**
     * @brief Fill wrapper pointers after library loading
     * @param functions Map of mangled names to function names
     * @param handlewp Wrapper library handle
     * @param handle Main library handle
     * @param config Wrapper configuration
     */
    static void fillWrapperPtrs(
        const std::unordered_map<std::string, std::string>& functions,
        void* handlewp, void* handle, WrapperConfig& config);
        
    /**
     * @brief Callback for dynamic symbol resolution
     * Called by trampolines when they need to resolve a symbol
     * @param ptr Pointer to function pointer
     * @param name Symbol name to resolve
     * @param config Wrapper configuration
     */
    static void loadSymbolToPtr(void** ptr, const char* name, 
                               const WrapperConfig& config);
};

/**
 * @brief C interface for assembly trampolines
 * Bridge between assembly code and C++ symbol resolution
 * @param ptr Pointer to function pointer to update
 * @param name Symbol name to resolve
 */
extern "C" void loadfnToPtr(void** ptr, const char* name);

}
```

## AST Context API

### AstContext Class (analysis/ast_context.hpp)

```cpp
namespace analysis {

/**
 * @brief Thread-safe context for AST analysis operations
 * Replaces global variables with encapsulated state
 */
class AstContext {
public:
    AstContext() = default;
    ~AstContext() = default;
    
    // Non-copyable, but movable
    AstContext(const AstContext&) = delete;
    AstContext& operator=(const AstContext&) = delete;
    AstContext(AstContext&&) = default;
    AstContext& operator=(AstContext&&) = default;
    
    /**
     * @brief Add an include to the output header
     * @param includePath Path of the include file
     */
    void addInclude(const std::string& includePath);
    
    /**
     * @brief Add an extern declaration to the output header
     * @param declaration Declaration to add
     */
    void addDeclaration(const std::string& declaration);
    
    /**
     * @brief Add a #line directive to the output header
     * @param line Line number
     * @param file Source file
     */
    void addLineDirective(int64_t line, const std::filesystem::path& file);
    
    /**
     * @brief Get the complete output header content
     * @return Header content with all includes and declarations
     */
    std::string getOutputHeader() const;
    
    /**
     * @brief Check if a file has been included
     * @param filepath Path to check
     * @return true if file is already included
     */
    bool isFileIncluded(const std::filesystem::path& filepath) const;
    
    /**
     * @brief Get set of all included files
     * @return Set of included file paths
     */
    std::unordered_set<std::string> getIncludedFiles() const;
    
    /**
     * @brief Clear all included files
     */
    void clearIncludedFiles();
    
    /**
     * @brief Export context to header file
     * @param path Output file path
     */
    void exportToHeaderFile(const std::filesystem::path& path) const;
    
    /**
     * @brief Merge another context into this one
     * @param other Context to merge from
     */
    void mergeFrom(const AstContext& other);
};

}
```

## Command System API

### Built-in REPL Commands

The REPL system includes the following built-in commands:

#### Include System Commands
```cpp
/**
 * @brief Include a header file with compiler validation
 * @syntax #include <header> or #include "file"
 * @warning For .cpp files with global variables, prefer #eval to prevent double-free issues
 */
#include <vector>        // ✅ Safe: Standard library headers  
#include "header.h"      // ✅ Safe: Custom headers
#include "globals.cpp"   // ❌ DANGEROUS: Use #eval instead

/**
 * @brief Add include directory to compiler search path
 * @syntax #includedir <path>
 */
#includedir /usr/local/include

/**
 * @brief Evaluate C++ file with proper extern declaration handling
 * @syntax #eval <file>
 * @note Preferred over #include for .cpp files with global variables
 */
#eval mycode.cpp         // ✅ Safe: REPL manages global state properly
```

#### Build Configuration Commands
```cpp
/**
 * @brief Link with library
 * @syntax #lib <name>
 */
#lib pthread

/**
 * @brief Add preprocessor definition
 * @syntax #compilerdefine <definition>
 */
#compilerdefine DEBUG=1
```

### CommandRegistry Class (commands/command_registry.hpp)

```cpp
namespace commands {

/**
 * @brief Base class for command context data
 */
struct CommandContextBase {
    virtual ~CommandContextBase() = default;
};

/**
 * @brief Function type for command handlers
 * @param arg Command argument string
 * @param ctx Command context containing state
 * @return true if command handled successfully
 */
using CommandHandler = std::function<bool(std::string_view, CommandContextBase&)>;

/**
 * @brief Registry entry for a command
 */
struct CommandEntry {
    std::string prefix;      ///< Command prefix (e.g., "#includedir ")
    std::string description; ///< Human-readable description
    CommandHandler handler;  ///< Handler function
};

/**
 * @brief Registry for REPL commands with plugin-style architecture
 */
class CommandRegistry {
public:
    /**
     * @brief Register a new command prefix
     * @param prefix Command prefix string
     * @param description Command description
     * @param handler Function to handle the command
     */
    void registerPrefix(std::string prefix, std::string description, 
                       CommandHandler handler);
    
    /**
     * @brief Try to handle a command line
     * @param line Input line to check
     * @param ctx Context for command execution
     * @return true if command was handled
     */
    bool tryHandle(std::string_view line, CommandContextBase& ctx) const;
    
    /**
     * @brief Get all registered command entries
     * @return Vector of command entries
     */
    const std::vector<CommandEntry>& entries() const;
};

/**
 * @brief Get global command registry instance
 * @return Reference to singleton registry
 */
CommandRegistry& registry();

/**
 * @brief Template wrapper for typed command contexts
 */
template<typename Context>
struct BasicContext : public CommandContextBase {
    Context data;
    explicit BasicContext(Context d) : data(std::move(d)) {}
};

/**
 * @brief Handle command with typed context
 * @param line Command line to process
 * @param context Typed context data
 * @return true if command was handled
 */
template<typename Context>
bool handleCommand(std::string_view line, Context& context);

}
```

### REPL Commands (commands/repl_commands.hpp)

```cpp
namespace repl_commands {

/**
 * @brief Context view for REPL command handlers
 */
struct ReplCtxView {
    std::unordered_set<std::string>* includeDirectories;     ///< Include paths
    std::unordered_set<std::string>* preprocessorDefinitions; ///< Preprocessor defs
    std::unordered_set<std::string>* linkLibraries;          ///< Link libraries
    bool* useCpp2Ptr;                                        ///< cpp2 mode flag
};

/**
 * @brief Register all built-in REPL commands
 * @param view Context view for command access (can be nullptr)
 */
void registerReplCommands(ReplCtxView* view);

/**
 * @brief Handle REPL command with context
 * @param line Command line to process
 * @param view Context view for state access
 * @return true if command was handled successfully
 */
bool handleReplCommand(std::string_view line, ReplCtxView view);

}
```

## Completion System API

### ClangCompletion (completion/clang_completion.hpp)

```cpp
namespace completion {

/**
 * @brief Single completion item
 */
struct CompletionItem {
    std::string text;          ///< Completion text
    std::string display;       ///< Display text  
    std::string documentation; ///< Documentation string
    std::string returnType;    ///< Return type (for functions)
    int priority{};           ///< Completion priority
    
    enum class Kind {
        Variable, Function, Class, Struct, Enum, Keyword, Include, Macro
    } kind{Kind::Variable};
};

/**
 * @brief Context for completion requests
 */
struct ReplContext {
    std::string currentIncludes;      ///< Current include statements
    std::string variableDeclarations; ///< Current variable declarations
    std::string functionDeclarations; ///< Current function declarations
    std::vector<std::string> includePaths; ///< Include search paths
};

/**
 * @brief Get completions at a specific position in code
 * @param code Source code string
 * @param line Line number (1-based)
 * @param column Column number (1-based)
 * @param context REPL context for completion
 * @return Vector of completion items
 */
std::vector<CompletionItem> getCompletions(const std::string& code,
                                          unsigned line, unsigned column,
                                          const ReplContext& context);

/**
 * @brief Initialize completion system
 * @return true if initialization successful
 */
bool initializeCompletion();

/**
 * @brief Shutdown completion system
 */
void shutdownCompletion();

}
```

## Utility APIs

### File RAII (utility/file_raii.hpp)

```cpp
namespace utility {

/**
 * @brief RAII wrapper for FILE* operations
 */
struct FileRAII {
    std::unique_ptr<FILE, int(*)(FILE*)> file;
    
    explicit FileRAII(const char* filename, const char* mode);
    
    FILE* get() const { return file.get(); }
    explicit operator bool() const { return file != nullptr; }
};

/**
 * @brief RAII wrapper for popen() operations
 * @param command Command to execute
 * @param mode Mode string ("r" or "w")
 * @return RAII-managed FILE pointer
 */
FileRAII make_popen(const char* command, const char* mode);

}
```

### Library Introspection (utility/library_introspection.hpp)

```cpp
namespace utility {

/**
 * @brief Get variable declarations from built library file
 * @param filepath Path to library file
 * @return Vector of variable declarations found
 */
std::vector<VarDecl> getBuiltFileDecls(const std::string& filepath);

}
```

### Assembly Information (utility/assembly_info.hpp)

```cpp
namespace utility {

/**
 * @brief Get assembly information for debugging crashes
 * @param addr Address to analyze
 * @return Assembly instruction and context information
 */
std::string getAssemblyInfo(void* addr);

}
```

## Configuration Structures

### BuildSettings

```cpp
/**
 * @brief Global build configuration state
 * Contains all compiler and linker settings for the REPL
 */
struct BuildSettings {
    std::unordered_set<std::string> includeDirectories;
    std::unordered_set<std::string> linkLibraries;
    std::unordered_set<std::string> preprocessorDefinitions;
    std::unordered_set<std::string> linkedLibrariesSet;
    std::unordered_set<std::string> sources;
    std::unordered_set<std::string> alreadyProcessed;
    
    // Thread-safe access methods
    std::string getLinkLibrariesStr() const;
    std::string getIncludeDirectoriesStr() const;
    std::string getPreprocessorDefinitionsStr() const;
};
```

### ReplState

```cpp
/**
 * @brief Current REPL session state
 */
struct ReplState {
    std::vector<VarDecl> allVars;              ///< All declared variables
    std::unordered_set<std::string> fnNames;   ///< Function names
    std::unordered_set<std::string> varNames;  ///< Variable names
    std::string lastError;                     ///< Last error message
    bool useCpp2 = false;                     ///< cpp2 syntax mode
    
    // State management
    void addVariable(const VarDecl& var);
    bool hasVariable(const std::string& name) const;
    VarDecl getVariable(const std::string& name) const;
};
```

## Error Handling Patterns

### Modern Error Handling

The system uses **std::expected**-style error handling throughout:

```cpp
// Check for errors
auto result = compilerService.buildLibraryOnly(compiler, name);
if (!result) {
    switch (result.error) {
        case CompilerError::BuildFailed:
            std::cerr << "Build failed\n";
            break;
        case CompilerError::FileWriteFailed:
            std::cerr << "Cannot write file\n"; 
            break;
        // ... handle other cases
    }
    return;
}

// Use successful result
int returnCode = *result;
```

### Exception Safety

All classes provide **strong exception safety** guarantee:

```cpp
class ExceptionSafeComponent {
    std::vector<Resource> resources_;
    mutable std::mutex mutex_;
    
public:
    void addResource(Resource&& resource) {
        std::scoped_lock lock(mutex_);
        
        // Strong exception safety: either succeeds completely or no change
        std::vector<Resource> temp = resources_;
        temp.push_back(std::move(resource));
        
        // Only update if no exception thrown
        resources_ = std::move(temp);
    }
};
```

## Threading and Concurrency

### Thread Safety Guarantees

**All public APIs are thread-safe** using these patterns:

1. **Shared/Exclusive Locking:**
```cpp
mutable std::shared_mutex mutex_;

// Read operations (multiple concurrent readers)
std::shared_lock<std::shared_mutex> lock(mutex_);

// Write operations (exclusive access)
std::scoped_lock<std::shared_mutex> lock(mutex_);
```

2. **Stateless Design:**
```cpp
// CompilerService is stateless - thread-safe by design
class CompilerService {
    const BuildSettings* buildSettings_; // Read-only reference
    // No mutable state
};
```

3. **Immutable Data:**
```cpp
// Pass by const reference or value for thread safety
CompilerResult<int> buildLibrary(const std::string& compiler, 
                                const std::string& name) const;
```

## Performance Optimization

### Parallel Processing

**Multi-threaded Compilation:**
```cpp
// Automatically scales to hardware_concurrency()
auto result = compilerService.buildMultipleSourcesWithAST(
    compiler, libname, sources, std);
    
// Measured performance: 93ms average compilation
```

### Caching Strategies

**String-based Cache Matching:**
- Identical code strings reuse compiled libraries
- Cache hit: ~1-15μs execution time
- Cache miss: ~50-500ms compilation time

### Memory Management

**RAII Patterns Throughout:**
```cpp
// Automatic resource management
auto file = utility::make_popen(command, "r");
if (file) {
    // Use file.get()
    // Automatic cleanup when scope exits
}
```

---

## Command Line Interface

### Main Program (main.cpp)

```cpp
/**
 * @brief Main program entry point
 * @param argc Argument count
 * @param argv Argument vector
 * @return Exit code
 */
int main(int argc, char** argv);
```

**Supported Flags:**
- `-h, --help` - Show usage information
- `-V, --version` - Show version information  
- `-s, --safe` - Enable signal handlers for crash protection
- `-r, --run FILE` - Execute REPL commands from file (batch mode)
- `-v, --verbose` - Increase verbosity level (repeatable)
- `-q, --quiet` - Suppress non-error output

### Batch Processing

**File Format for `-r` option:**
```cpp
// Comments supported
#includedir /usr/local/include
#lib pthread

// Standard C++ code
#include <iostream>
int main() {
    std::cout << "Hello from batch mode!\n";
    return 0;
}

// REPL commands
#return 42 * 2
```

## Signal Handling System

### Hardware Exception Translation

**Signal Handlers (main.cpp):**
```cpp
/**
 * @brief Handle segmentation fault signals
 * @param info Hardware exception information
 * @throws segvcatch::segmentation_fault
 */
void handle_segv(const segvcatch::hardware_exception_info& info);

/**
 * @brief Handle floating point exception signals  
 * @param info Hardware exception information
 * @throws segvcatch::floating_point_error
 */
void handle_fpe(const segvcatch::hardware_exception_info& info);

/**
 * @brief Handle illegal instruction signals
 * @param info Hardware exception information
 * @throws segvcatch::illegal_instruction  
 */
void handle_sigill(const segvcatch::hardware_exception_info& info);
```

**Stack Trace Generation:**
The system provides automatic stack trace analysis with:
- Symbol resolution via `dladdr()`
- Assembly instruction analysis via `objdump`
- Source line mapping via `addr2line`
- Library offset calculation

---

*This API reference is generated from actual source code analysis. For implementation details, see the corresponding source files in `include/` and `src/` directories.*