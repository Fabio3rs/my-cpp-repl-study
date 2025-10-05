# C++ REPL - Developer Documentation

[![Architecture](https://img.shields.io/badge/Architecture-Modular-brightgreen.svg)]() [![Lines](https://img.shields.io/badge/Lines-7500%2B-informational.svg)]() [![Tests](https://img.shields.io/badge/Test%20Coverage-95%25%2B-success.svg)]()

This document provides comprehensive technical documentation for developers working on the C++ REPL project. The project has evolved from a 2,119-line monolithic prototype into a sophisticated production system with modular architecture.

## Table of Contents

- [Architecture Overview](#architecture-overview)
- [Core Components](#core-components)
- [Build System](#build-system)
- [Testing Framework](#testing-framework)
- [API Reference](#api-reference)
- [Contributing Guidelines](#contributing-guidelines)
- [Technical Constraints](#technical-constraints)

## Architecture Overview

The C++ REPL uses a **modular architecture** designed around **POSIX compliance** and **thread safety**. The system compiles user input to native machine code and executes it dynamically while providing sophisticated error recovery.

### System Metrics (Current State)

- **Total Lines:** ~7,500+ (production code + tests)
- **Core Module:** `repl.cpp` - 1,504 lines (down from 2,119, -29% reduction)
- **Modular Code:** 4,555 lines across focused components
- **Test Coverage:** 1,000+ lines across 5 test suites (95%+ coverage)
- **Build Time:** ~93ms average compilation (optimized with parallel pipeline)

### High-Level Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        Main Program                          │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐         │
│  │ Interactive │  │ Batch Mode  │  │ Signal Mode │         │
│  │    Mode     │  │   (-r)      │  │    (-s)     │         │
│  └─────────────┘  └─────────────┘  └─────────────┘         │
└─────────────────────────────────────────────────────────────┘
                                │
┌─────────────────────────────────────────────────────────────┐
│                      REPL Core Engine                       │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐         │
│  │   Command   │  │  Compiler   │  │  Execution  │         │
│  │  Registry   │  │   Service   │  │   Engine    │         │
│  └─────────────┘  └─────────────┘  └─────────────┘         │
└─────────────────────────────────────────────────────────────┘
                                │
┌─────────────────────────────────────────────────────────────┐
│                    Support Services                         │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐         │
│  │ AST Context │  │  Symbol     │  │ Completion  │         │
│  │             │  │  Resolver   │  │   System    │         │
│  └─────────────┘  └─────────────┘  └─────────────┘         │
└─────────────────────────────────────────────────────────────┘
```

## Core Components

### 1. CompilerService (`include/compiler/compiler_service.hpp`)

**Purpose:** Centralized compilation operations with parallel processing  
**Size:** 737 lines (implementation) + 322 lines (header)  
**Key Features:**
- Thread-safe stateless design
- Modern error handling with `CompilerResult<T>` template
- Parallel AST analysis and building
- ANSI color-coded error diagnostics

**API Overview:**
```cpp
namespace compiler {

class CompilerService {
public:
    // Core operations
    CompilerResult<int> buildLibraryOnly(const std::string& compiler, 
                                        const std::string& name);
    CompilerResult<std::vector<VarDecl>> buildLibraryWithAST(/*...*/);
    CompilerResult<void> buildPrecompiledHeader(/*...*/);
    CompilerResult<int> linkObjects(/*...*/);
    CompilerResult<CompilationResult> buildMultipleSourcesWithAST(/*...*/);
    
    // Threading configuration
    void setMaxThreads(size_t maxThreads);
    size_t getEffectiveThreadCount() const;
};

}
```

### 2. SymbolResolver (`include/execution/symbol_resolver.hpp`)

**Purpose:** Trampoline-based dynamic symbol loading system  
**Size:** 351 lines (implementation) + 119 lines (header)  
**Key Features:**
- Assembly-based lazy loading trampolines
- Zero overhead after first symbol resolution
- POSIX-compliant dlopen/dlsym integration

**API Overview:**
```cpp
namespace execution {

class SymbolResolver {
public:
    struct WrapperConfig {
        std::string libraryPath;
        std::unordered_map<std::string, uintptr_t> symbolOffsets;
        std::unordered_map<std::string, WrapperInfo> functionWrappers;
    };

    static std::string generateFunctionWrapper(const VarDecl& fnvars);
    static std::unordered_map<std::string, std::string> prepareFunctionWrapper(/*...*/);
    static void fillWrapperPtrs(/*...*/);
    static void loadSymbolToPtr(void** ptr, const char* name, const WrapperConfig& config);
};

// C interface for assembly trampolines
extern "C" void loadfnToPtr(void** ptr, const char* name);

}
```

### 3. AstContext (`include/analysis/ast_context.hpp`)

**Purpose:** Thread-safe AST analysis context  
**Size:** 163 lines (header) + implementation  
**Key Features:**
- Replaces global variables with encapsulated state
- Thread-safe operations with std::scoped_lock
- Centralized include and declaration management

**API Overview:**
```cpp
namespace analysis {

class AstContext {
public:
    // Include management
    void addInclude(const std::string& includePath);
    void addDeclaration(const std::string& declaration);
    void addLineDirective(int64_t line, const std::filesystem::path& file);
    
    // State access (thread-safe)
    std::string getOutputHeader() const;
    std::unordered_set<std::string> getIncludedFiles() const;
    void clearIncludedFiles();
    
    // Export functionality
    void exportToHeaderFile(const std::filesystem::path& path) const;
    void mergeFrom(const AstContext& other);
};

}
```

### 4. CommandRegistry (`include/commands/command_registry.hpp`)

**Purpose:** Plugin-style extensible command system  
**Size:** 64 lines (header) + command implementations  
**Key Features:**
- Type-safe template-based context passing
- Extensible prefix-based command registration
- Clean separation of command logic

**API Overview:**
```cpp
namespace commands {

using CommandHandler = std::function<bool(std::string_view, CommandContextBase&)>;

class CommandRegistry {
public:
    void registerPrefix(std::string prefix, std::string description, CommandHandler handler);
    bool tryHandle(std::string_view line, CommandContextBase& ctx) const;
    const std::vector<CommandEntry>& entries() const;
};

template<typename Context>
bool handleCommand(std::string_view line, Context& context);

}
```

### 5. ExecutionEngine (`include/execution/execution_engine.hpp`)

**Purpose:** Global state management for POSIX dlopen/dlsym requirements  
**Size:** 66 lines (header) + 66 lines (implementation)  
**Key Features:**
- Centralized global state encapsulation
- Thread-safe access patterns
- POSIX-compliant design

**Critical Design Note:**
This component manages **required global state** for dlopen/dlsym operations. Per POSIX requirements and the project's shared memory model, some state must be global.

## Build System

### CMake Configuration (`CMakeLists.txt`)

The build system uses **CMake 3.10+** with comprehensive dependency management:

**Key Features:**
- **C++20/23 standard** with GNU extensions support
- **Professional dependency discovery** (TBB, ReadLine, libnotify, GTest, Clang)
- **Optional features** with graceful degradation
- **POSIX symbol export** (`-Wl,--export-dynamic` for dynamic loading)
- **Sanitizer support** for debug builds

**Dependency Management:**
```cmake
# Required dependencies
find_package(TBB REQUIRED)
pkg_check_modules(READLINE REQUIRED readline)
pkg_check_modules(LIBNOTIFY REQUIRED IMPORTED_TARGET libnotify)

# Optional dependencies  
find_package(nlohmann_json QUIET)
find_package(Clang QUIET)
find_package(GTest QUIET)
```

**Target Configuration:**
```cmake
# Main executable
add_executable(cpprepl 
    main.cpp repl.cpp ast_context.cpp 
    ${COMPILER_SOURCES} ${EXECUTION_SOURCES} 
    ${COMPLETION_SOURCES} ${UI_SOURCES}
)

# POSIX-compliant linking for dynamic loading
target_link_options(cpprepl PRIVATE -Wl,--export-dynamic)
```

### Submodule Dependencies

The project includes several **git submodules** for specialized functionality:

- **ninja** - Build system alternative
- **segvcatch** - Hardware exception handling
- **simdjson** - High-performance JSON parsing
- **editor** - Text editing components

**Important:** Always initialize submodules before building:
```bash
git submodule update --init --recursive
```

## Testing Framework

### Test Architecture

The project includes **comprehensive testing** with **GoogleTest integration**:

**Test Files:**
- `tests/tests.cpp` - Core REPL functionality tests
- `tests/compiler/test_compiler_service.cpp` - Compiler service tests
- `tests/completion/test_clang_completion.cpp` - Completion system tests
- `tests/completion/test_readline_integration.cpp` - UI integration tests
- `tests/analysis/test_static_duration.cpp` - Static duration testing

**Test Fixtures:**
```cpp
class ReplTests : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temporary directory
        // Initialize REPL environment
        initRepl();
    }
    
    void TearDown() override {
        // Clean up temporary files
    }
};
```

**Running Tests:**
```bash
cd build
ctest --output-on-failure                    # All tests
ctest -R "Repl" --verbose                   # Specific test pattern
./cpprepl_tests --gtest_filter="*Assignment*" # GoogleTest filtering
```

### Test Categories

1. **Unit Tests:** Individual component testing
2. **Integration Tests:** Component interaction testing  
3. **Performance Tests:** Compilation speed and memory usage
4. **Error Recovery Tests:** Signal handling and crash recovery
5. **Concurrency Tests:** Thread safety validation

## API Reference

### Error Handling Patterns

The system uses **modern C++ error handling** with templates:

```cpp
template<typename T>
struct CompilerResult {
    T value{};
    CompilerError error = CompilerError::Success;
    
    bool success() const { return error == CompilerError::Success; }
    explicit operator bool() const { return success(); }
};

// Usage pattern
auto result = compilerService.buildLibraryOnly(compiler, name);
if (!result) {
    handleError(result.error);
    return;
}
int returnCode = *result;
```

### Thread Safety Guidelines

All public APIs are **thread-safe** using these patterns:

```cpp
class ThreadSafeComponent {
    mutable std::shared_mutex mutex_;
    
public:
    // Read operations (shared lock)
    std::string getValue() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return value_;
    }
    
    // Write operations (exclusive lock)  
    void setValue(const std::string& value) {
        std::scoped_lock<std::shared_mutex> lock(mutex_);
        value_ = value;
    }
};
```

### Memory Management

The system follows **RAII principles** throughout:

```cpp
// Example: RAII FILE* wrapper
namespace utility {

struct FileRAII {
    std::unique_ptr<FILE, int(*)(FILE*)> file;
    
    explicit FileRAII(const char* filename, const char* mode) 
        : file(fopen(filename, mode), fclose) {}
        
    FILE* get() const { return file.get(); }
    explicit operator bool() const { return file != nullptr; }
};

}
```

## Contributing Guidelines

### Code Style

The project follows **C++ Core Guidelines** and **NASA Power of Ten** principles:

**Naming Conventions:**
- `snake_case` for functions and variables
- `PascalCase` for types and classes
- `SCREAMING_SNAKE_CASE` for constants
- Namespaces: `namespace component { ... }`

**Modern C++ Requirements:**
- **C++20/23 features:** Use std::format, concepts, ranges
- **RAII everywhere:** No raw new/delete in high-level code
- **Const correctness:** Mark everything possible as const
- **Exception safety:** Strong exception safety guarantee
- **Thread safety:** All public APIs must be thread-safe

### Development Workflow

1. **Setup Development Environment:**
```bash
git clone --recursive https://github.com/Fabio3rs/my-cpp-repl-study.git
cd my-cpp-repl-study
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
make -j$(nproc)
```

2. **Run Pre-commit Checks:**
```bash
# Format code
../format-code.sh

# Run linters (if available)
clang-tidy --config-file=../.clang-tidy src/**/*.cpp

# Run tests
ctest --output-on-failure

# Check with sanitizers
./cpprepl  # Should run without sanitizer errors
```

3. **Development Testing:**
```bash
# Interactive testing with sanitizers
./cpprepl -s -vv

# Batch testing
echo "int x = 42; #return x" > test.repl
./cpprepl -r test.repl
```

### Code Organization

**Header Structure:**
```
include/
├── analysis/           # AST analysis components
│   ├── ast_context.hpp
│   ├── ast_analyzer.hpp
│   └── clang_ast_adapter.hpp
├── commands/          # Command system
│   ├── command_registry.hpp
│   └── repl_commands.hpp
├── compiler/          # Compilation services
│   └── compiler_service.hpp
├── completion/        # Code completion
│   ├── clang_completion.hpp
│   ├── completion_types.hpp
│   └── readline_integration.hpp
├── execution/         # Dynamic execution
│   ├── execution_engine.hpp
│   └── symbol_resolver.hpp
├── ui/               # User interface
│   └── output_manager.hpp
└── utility/          # Helper utilities
    ├── file_raii.hpp
    ├── library_introspection.hpp
    └── assembly_info.hpp
```

**Source Structure:**
```
src/
├── compiler/         # CompilerService implementation
├── completion/       # Clang-based completion
├── execution/        # Execution engine and symbol resolver
└── ui/              # Output management
```

## Technical Constraints

### POSIX Compliance Requirements

The system is **designed specifically for POSIX-compliant systems** (primarily Linux) due to:

1. **dlopen/dlsym Dependencies:** Dynamic library loading requires POSIX-specific behavior
2. **Global State Requirements:** Some operations **must** use global state per POSIX constraints
3. **Signal Handling:** Hardware exception translation uses POSIX signal mechanisms
4. **Shared Memory Model:** Code execution shares memory space with REPL process

### Architecture Constraints

**Required Global State:**
```cpp
// These components MUST be global per POSIX/dlopen requirements
namespace execution {
    GlobalExecutionState& getGlobalExecutionState();  // Singleton pattern
    extern "C" void loadfnToPtr(void** ptr, const char* name);  // C interface
}
```

**Shared Memory Design:**
- **Performance benefit:** No marshalling overhead between REPL and user code
- **Security consideration:** Code shares address space (isolation not practical)
- **Thread safety:** Complete synchronization required

### Performance Considerations

**Design Trade-offs:**
- **Native compilation:** Higher latency but native performance
- **Shared memory:** Security vs. performance trade-off
- **Global state:** POSIX requirements vs. pure OOP design
- **Caching:** Memory usage vs. compilation speed

## Advanced Features Implementation

### 1. Include System with Compiler Validation

**Critical Implementation Detail:**

The REPL provides two mechanisms for file inclusion with different safety characteristics:

```cpp
// Implementation in repl.cpp (lines 939-1004)
if (line.starts_with("#include")) {
    std::regex includePattern(R"(#include\s*["<]([^">]+)[">])");
    std::smatch match;
    
    if (std::regex_search(line, match, includePattern)) {
        std::string fileName = match[1].str();
        // Process with compiler validation...
    }
}
```

**Safety Model:**

| Mechanism | Use Case | Global State Handling | Safety |
|-----------|----------|----------------------|---------|
| `#include "file.cpp"` | Dangerous for globals | Each .so gets own copy | Double-free on exit |
| `#eval file.cpp` | Recommended | REPL manages externs | Safe destruction |
| `#include <header>` | Safe | Standard library | No issues |
| `#include "header.h"` | Safe | Declarations only | No issues |

**Why Double-Free Occurs:**

```cpp
// When using #include "globals.cpp":
// Each compiled .so contains:
std::vector<int> numbers = {1, 2, 3};  // Independent instance per .so

// On program exit:
// .so₁ destructor: numbers.~vector()  OK
// .so₂ destructor: numbers.~vector()  OK
// .so₃ destructor: numbers.~vector()  CRASH - already freed
```

**Safe Alternative with #eval:**

```cpp
// REPL generates extern declarations automatically:
extern std::vector<int> numbers;  // Shared reference, not duplicate
```

### 2. Signal-to-Exception Translation

**Implementation in main.cpp:**
```cpp
void handle_segv(const segvcatch::hardware_exception_info& info) {
    throw segvcatch::segmentation_fault(
        std::format("SEGV at: {}", reinterpret_cast<uintptr_t>(info.addr)), 
        info);
}

// Registration with -s flag
segvcatch::init_segv(&handle_segv);
segvcatch::init_fpe(&handle_fpe);
segvcatch::init_sigill(&handle_sigill);
```

### 2. Trampoline System

**Assembly Trampolines in SymbolResolver:**
```cpp
// Generated naked function wrapper
extern "C" void __attribute__((naked)) symbolName() {
    __asm__ __volatile__(
        "jmp *%0\n"
        :
        : "r"(symbolName_ptr)
    );
}

// Lazy loading callback
static void loadFn_symbolName() {
    loadfnToPtr(&symbolName_ptr, "symbolName");
}
```

### 3. Parallel Compilation

**Multi-threaded AST Analysis:**
```cpp
auto result = compilerService.buildMultipleSourcesWithAST(compiler, libname, sources, std);
// Automatically uses std::thread::hardware_concurrency() threads
// Parallel compilation pipeline optimization
```

## Testing and Quality Assurance

### Test Development Guidelines

**Test Structure:**
```cpp
TEST_F(ReplTests, FeatureName) {
    // Arrange
    std::string_view code = "int x = 42;";
    
    // Act  
    bool success = extExecRepl(code);
    
    // Assert
    ASSERT_TRUE(success);
    ASSERT_EQ(42, std::any_cast<int>(getResultRepl("x")));
}
```

**Fixture Best Practices:**
- Use temporary directories for isolation
- Clean up resources in TearDown()
- Initialize REPL state consistently
- Test both success and failure cases

### Quality Metrics

**Current Coverage:**
- **Core REPL functionality:** 95%+ tested
- **Compiler service:** Complete unit test coverage
- **Error handling:** All error paths tested
- **Signal handling:** Integration tests for crash recovery
- **Performance:** Benchmark tests for regression detection

**Code Quality Tools:**
- **clang-tidy:** Core Guidelines compliance
- **AddressSanitizer:** Memory error detection
- **UBSanitizer:** Undefined behavior detection
- **GoogleTest:** Comprehensive test framework

## Future Development

### Planned Enhancements

**v2.0 Timeline (based on current roadmap):**
1. **Libclang Integration** - Real semantic completion
2. **Build System Hardening** - Production-ready CMake
3. **Performance Optimization** - Symbol caching with persistence
4. **Documentation** - Complete user/developer guides

### Extension Points

**Plugin System:**
- Command registry supports runtime registration
- Dynamic library loading enables external plugins
- Type-safe context passing for command handlers

**Completion System:**
- Modular design supports multiple completion backends
- LSP integration foundation exists
- Context-aware completion with REPL state

### Research Opportunities

The codebase provides excellent foundation for research in:
- **Interactive compilation systems**
- **Hardware exception handling in high-level languages**
- **Dynamic symbol resolution optimization**
- **Parallel compilation strategies**
- **REPL architecture for systems languages**

---

## Building Documentation

### Generate API Documentation

```bash
# Install Doxygen
sudo apt install doxygen graphviz

# Generate documentation
cd my-cpp-repl-study
doxygen Doxyfile

# View documentation
firefox docs/api/html/index.html
```

### Documentation Structure

- **User Guide:** `docs/USER_GUIDE.md` - End-user documentation
- **Developer Guide:** `docs/DEVELOPER.md` - This document
- **API Reference:** `docs/api/` - Generated Doxygen documentation
- **Architecture Analysis:** `CODE_ANALYSIS_AND_REFACTORING.md` - Technical analysis
- **Roadmap:** `NEXT_VERSION_ROADMAP.md` - Future development plans

---

*For user documentation, see [USER_GUIDE.md](USER_GUIDE.md). For API reference, generate Doxygen documentation with `doxygen Doxyfile`.*