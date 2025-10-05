
# User guide

This document is the user guide for the C++ REPL v1.5-alpha. The README contains a short project summary; this guide provides step-by-step instructions, usage examples and troubleshooting tips.

## Current Features (v1.5-alpha)

- **Interactive C++ execution** - Line-by-line compilation and execution
- **Variable persistence** - Variables maintain state across REPL sessions
- **Signal handling** (`-s` flag) - Graceful recovery from crashes
- **Batch processing** (`-r` flag) - Execute C++ command files
- **Simple autocompletion** - Basic keyword and symbol completion
- **Include support** - `#include` directive functionality
- **Plugin system** - Load pre-built libraries with `#loadprebuilt`

## Planned for v2.0 (high level)

- LSP-based semantic completion (clangd/libclang integration)
- Improved error diagnostics with richer context
- Better multi-file project support

## Table of Contents

- [Quick Start](#quick-start)
- [Installation](#installation)
- [Basic Usage](#basic-usage)
- [Command Reference](#command-reference)
- [Additional Features](#additional-features)
- [Performance](#performance)
- [Troubleshooting](#troubleshooting)

git clone --recursive https://github.com/Fabio3rs/my-cpp-repl-study.git
## Quick start

```bash
# Clone and build
git clone --recursive https://github.com/Fabio3rs/my-cpp-repl-study.git
cd my-cpp-repl-study
mkdir build && cd build
cmake ..
make -j$(nproc)   # or: ninja -j$(nproc)

# Start the REPL
./cpprepl

# Try some simple C++ code interactively:
int x = 42;
#return x * 2
```

## Installation (summary)

Prerequisites (summary): Linux (POSIX), CMake 3.10+, Clang/LLVM (10+ recommended), `pkg-config`, and development headers for `readline` and `tbb`.

On Ubuntu/Debian you can install common dependencies with:

```bash
sudo apt update
sudo apt install build-essential clang cmake pkg-config \
  libreadline-dev libtbb-dev libnotify-dev libgtest-dev \
  ninja-build
```

See `docs/INSTALLATION.md` for a more detailed, step-by-step installation guide and optional dependency list.

### Build Process

```bash
# 1. Clone with submodules
git clone --recursive https://github.com/Fabio3rs/my-cpp-repl-study.git
cd my-cpp-repl-study

# 2. Initialize submodules (if not done with --recursive)
git submodule update --init --recursive

# 3. Configure build
mkdir build && cd build
cmake .. [OPTIONS]

# 4. Build
make -j$(nproc)
# OR with Ninja
ninja

# 5. Run tests (optional)
ctest --output-on-failure
```

**Build Options:**
```bash
cmake .. \
  -DENABLE_NOTIFICATIONS=ON \    # Desktop notifications (default: ON)
  -DENABLE_ICONS=ON \            # Icon support (default: ON)
  -DENABLE_SANITIZERS=OFF \      # Address/UB sanitizers (default: OFF)
  -DCMAKE_BUILD_TYPE=Release     # Release/Debug/RelWithDebInfo
```

## Basic Usage

### Interactive Mode (Default)

Start the REPL and begin coding immediately:

```bash
./cpprepl
```

**Example Session:**
```cpp
>>> int x = 42;
✓ Compiled successfully (93ms)

>>> std::vector<int> numbers = {1, 2, 3, 4, 5};
✓ Compiled successfully (95ms)

>>> #return std::accumulate(numbers.begin(), numbers.end(), 0)
15

>>> void greet(const std::string& name) {
...     std::cout << "Hello, " << name << "!\n";
... }
✓ Compiled successfully (156ms)

>>> greet("World")
Hello, World!

>>> exit
```

### Batch Mode

Execute REPL commands from a file:

```bash
./cpprepl -r script.repl
```

**Example script.repl:**
```cpp
// Setup
#includedir /usr/local/include
#lib pthread

// Code
#include <thread>
#include <chrono>

void delayed_hello() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::cout << "Hello from thread!\n";
}

std::thread t(delayed_hello);
t.join();
```

### Signal Handler Mode

Enable hardware exception protection for crash recovery:

```bash
./cpprepl -s
```

**Example with crash recovery:**
```cpp
>>> int* bad_ptr = nullptr;
>>> *bad_ptr = 42;  // This would normally crash
WARNING: SEGV at: 0x7f8b2c0015a0
Crash recovered - REPL continues normally
Stack trace: [automatic analysis provided]

>>> int good_value = 42;  // REPL continues working
✓ Compiled successfully (45ms)
```

## Command Reference

### Core Commands

| Command | Description | Example |
|---------|-------------|---------|
| `#includedir <path>` | Add include directory | `#includedir /usr/local/include` |
| `#include <header>` | Include header file with compiler validation | `#include <vector>` |
| `#include "file"` | Include local file with compiler validation | `#include "myheader.h"` |
| `#lib <name>` | Link with library | `#lib pthread` |
| `#compilerdefine <def>` | Add preprocessor definition | `#compilerdefine DEBUG=1` |
| `#loadprebuilt <path>` | Load prebuilt library | `#loadprebuilt ./mylib.so` |
| `#eval <file>` | Execute C++ file with extern declaration handling | `#eval mycode.cpp` |
| `#return <expr>` | Evaluate and print expression | `#return x + y` |
| `#lazyeval <code>` | Lazy evaluation mode | `#lazyeval func_call()` |
| `#batch_eval <file>` | Batch evaluation | `#batch_eval commands.txt` |

### Mode Commands

| Command | Description | Example |
|---------|-------------|---------|
| `#cpp2` | Enable cpp2 syntax mode | `#cpp2` |
| `#cpp1` | Disable cpp2 syntax mode | `#cpp1` |

### Utility Commands

| Command | Description | Example |
|---------|-------------|---------|
| `#help` | Show command help | `#help` |
| `#version` | Show version information | `#version` |
| `#status` | Show system status | `#status` |
| `#welcome` | Show welcome message | `#welcome` |
| `#clear` | Clear screen | `#clear` |
| `printall` | Show all variables | `printall` |
| `evalall` | Execute lazy evaluations | `evalall` |
| `exit` | Exit REPL | `exit` |

### Special Syntax

**Multiline Support:**
```cpp
>>> class MyClass {
... public:
...     int value;
...     MyClass(int v) : value(v) {}
... };
✓ Compiled successfully (234ms)
```

**Include Detection:**
```cpp
>>> #include <algorithm>
>>> #include <numeric>
✓ Headers processed automatically with compiler validation
```

## CRITICAL: Include vs Eval Usage

### Using `#include` vs `#eval` - Important Distinction

**For .cpp files with global variables, prefer `#eval` over `#include`:**

**DON'T DO THIS** - Causes double-free errors:
```cpp
// file: globals.cpp
std::vector<int> numbers = {1, 2, 3};

// In REPL:
>>> #include "globals.cpp"  // DANGEROUS!
```

**DO THIS INSTEAD** - Safe approach:
```cpp
// file: globals.cpp
std::vector<int> numbers = {1, 2, 3};

// In REPL:
>>> #eval globals.cpp       // SAFE!
```

**Why this matters:**

- `#include "file.cpp"`: Each .so instance gets its own copy of global variables
- On exit: Multiple destructors try to free the same resources → **double-free crash**
- `#eval file.cpp`: REPL manages extern declarations properly, preventing conflicts

**Safe Usage Guidelines:**

| File Type | Use | Reason |
|-----------|-----|--------|
| `.h/.hpp` headers | `#include` | Safe - declarations only |
| System headers `<header>` | `#include` | Safe - standard library |
| `.cpp` with globals | `#eval` | Safe - REPL manages extern declarations |
| `.cpp` without globals | Either | Safe - no shared state |

## Additional Features

### 1. Dynamic Library Loading

Load precompiled libraries at runtime:

```cpp
// Compile a library first
$ clang++ -shared -fPIC mylib.cpp -o mylib.so

// Load in REPL
>>> #loadprebuilt ./mylib.so
✓ Library loaded with 15 symbols

>>> my_library_function()  // Now available
```

### 2. Hardware Exception Handling

With `-s` flag, the REPL converts hardware faults to manageable exceptions:

```cpp
>>> int arr[5];
>>> arr[1000000] = 42;  // Out of bounds access
WARNING: SEGV at: 0x7f8b2c0015a0
Fault location: main+0x42 (myprogram.so)
Assembly: mov %eax,0x3d0900(%rax)
Execution recovered - continue coding
```

### 3. Variable Persistence

Variables persist across REPL sessions:

```cpp
>>> int global_counter = 0;
>>> void increment() { global_counter++; }
>>> increment(); increment();
>>> #return global_counter
2
```

### 4. Performance Optimization

The REPL includes intelligent caching:

- **String-based cache matching:** Identical code reuses compiled libraries
- **Parallel compilation:** Multi-core compilation for large code blocks
- **Precompiled headers:** Faster compilation for common includes
- **Symbol caching:** Optimized dynamic loading

### 5. Code Completion (Experimental)

When built with libclang support:

```cpp
>>> std::vec<TAB>
std::vector   std::vector_bool
>>> std::vector<int> v;
>>> v.<TAB>
push_back    pop_back    size    empty    begin    end    ...
```

## Performance

**Compilation Performance:**
- **Cache hit:** ~1-15μs execution time
- **New compilation:** ~50-500ms depending on complexity  
- **Parallel speedup:** Optimized compilation pipeline (93ms average)
- **Startup time:** ~0.82s with caching

**Memory Usage:**
- **Base footprint:** ~8-12MB
- **Peak memory:** ~150MB during complex compilations
- **Per compilation:** ~2-4MB (temporary)
- **Shared memory model:** Efficient code sharing

**Scalability:**
- **Thread safety:** Complete with std::scoped_lock
- **Multi-core:** Linear scaling with available cores
- **Large projects:** Handles complex codebases efficiently

## Command Line Options

```bash
cpprepl [OPTIONS]

Options:
  -h, --help      Show help information
  -V, --version   Show version information
  -s, --safe      Enable signal handlers for crash protection
  -r, --run FILE  Execute REPL commands from file (batch mode)
  -v, --verbose   Increase verbosity level (can be repeated)
  -q, --quiet     Suppress non-error output

Examples:
  cpprepl                    # Interactive mode
  cpprepl -s                 # Interactive with crash protection
  cpprepl -r script.repl     # Batch execution
  cpprepl -sv                # Safe mode with verbose output
```

## Troubleshooting

### Common Issues

**1. Compilation Errors**
```
ERROR: compilation failed
```
- Check C++ syntax
- Verify all includes are available
- Use `#includedir` for custom paths
- Check with `#status` for system state

**2. Library Loading Issues**
```
ERROR: cannot load library
```
- Verify library exists and has correct permissions
- Check library dependencies with `ldd`
- Ensure library compiled with `-fPIC`

**3. Memory Access Violations**
```
WARNING: SEGV at: 0x7f8b2c0015a0
```
- Use `-s` flag for crash protection
- Check array bounds and pointer validity
- Review stack trace provided

**4. Missing Dependencies**
```
ERROR: libclang not found
```
- Install development packages: `sudo apt install clang-dev libclang-dev`
- Reconfigure build: `cmake .. && make`

### Debug Mode

Enable verbose output for debugging:

```bash
# Level 1: Basic operation feedback
./cpprepl -v

# Level 2: Detailed compilation info  
./cpprepl -vv

# Level 3: Full debug output
./cpprepl -vvv
```

### Performance Tuning

**For Large Projects:**
```cpp
// Preload common headers
#includedir /usr/include/c++/11
#includedir /usr/local/include

// Batch operations
#batch_eval large_codebase.cpp
```

**For Development Workflows:**
```cpp
// Setup development environment
#lib stdc++fs
#lib pthread
#compilerdefine DEBUG=1
#includedir ./include
```

## Best Practices

### 1. Variable Management
- Use descriptive names for persistent variables
- Prefer const-correctness: `const int x = 42;`
- Use RAII patterns for resource management

### 2. Error Handling
- Always use `-s` flag for experimental code
- Check return values from functions
- Use smart pointers for dynamic allocation

### 3. Performance
- Group related declarations together
- Use `#lazyeval` for expensive operations
- Preload common libraries at session start

### 4. Code Organization
- Save complex code to files and use `#eval` for .cpp files
- Use `#include` only for headers (.h/.hpp) and system headers
- Use `#batch_eval` for project setup
- Organize includes at session beginning
- **NEVER use `#include` on .cpp files with global variables** - use `#eval` instead

## Integration Examples

### 1. Quick Prototyping
```cpp
>>> #includedir ./include
>>> #lib myproject
>>> #eval prototype.cpp
>>> test_my_algorithm()
```

### 2. Learning and Exploration
```cpp
>>> #include <algorithm>
>>> std::vector<int> data = {3, 1, 4, 1, 5};
>>> std::sort(data.begin(), data.end());
>>> #return data
[1, 1, 3, 4, 5]
```

### 3. Development Testing
```cpp
>>> #eval unit_test.cpp
>>> run_all_tests()
>>> #return test_results.passed
true
```

## Safety and Limitations

### What Works Safely
- Variable declarations and assignments
- Function definitions and calls
- STL container usage
- Dynamic library loading
- Exception handling
- Template instantiation

### Current Limitations
- **Shared memory model:** Code shares address space (security consideration)
- **Linux/POSIX only:** Windows support not implemented
- **Global state dependency:** Some operations require global state for dlopen/dlsym
- **Single session:** Multiple concurrent REPL instances not supported

### Safety Features
- **Hardware exception conversion:** SIGSEGV/SIGFPE/SIGILL → C++ exceptions
- **Graceful crash recovery:** Continue operation after errors
- **Stack trace analysis:** Automatic debugging information
- **Assembly introspection:** Detailed fault analysis
- **Thread safety:** Complete synchronization for concurrent operations

---

*For developer documentation and API reference, see [DEVELOPER.md](DEVELOPER.md) and generated Doxygen documentation in `docs/api/`.*