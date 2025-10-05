# C++ REPL - Quick Reference

## Command Line Usage

```bash
cpprepl [OPTIONS]

Options:
  -h, --help      Show help information
  -V, --version   Show version information
  -s, --safe      Enable signal handlers for crash protection
  -r, --run FILE  Execute REPL commands from file (batch mode)
  -v, --verbose   Increase verbosity level (can be repeated)
  -q, --quiet     Suppress non-error output
```

## REPL Commands

### Essential Commands
| Command | Purpose | Example |
|---------|---------|---------|
| `#includedir <path>` | Add include directory | `#includedir /usr/local/include` |
| `#include <header>` | Include header (compiler validated) | `#include <vector>` |
| `#include "file"` | Include local file - See safety note | `#include "header.h"` |
| `#lib <name>` | Link with library | `#lib pthread` |
| `#compilerdefine <def>` | Add preprocessor definition | `#compilerdefine DEBUG=1` |
| `#loadprebuilt <path>` | Load prebuilt library | `#loadprebuilt ./mylib.so` |
| `#eval <file>` | Execute C++ file (safe for globals) | `#eval mycode.cpp` |
| `#return <expr>` | Evaluate and print expression | `#return x + y` |

**SAFETY WARNING:** For .cpp files with global variables, use `#eval` instead of `#include` to prevent double-free crashes!

### Advanced Commands
| Command | Purpose | Example |
|---------|---------|---------|
| `#lazyeval <code>` | Lazy evaluation mode | `#lazyeval expensive_call()` |
| `#batch_eval <file>` | Batch evaluation | `#batch_eval setup.cpp` |
| `#cpp2` / `#cpp1` | Enable/disable cpp2 mode | `#cpp2` |

### Utility Commands
| Command | Purpose | Example |
|---------|---------|---------|
| `#help` | Show command help | `#help` |
| `#version` | Show version info | `#version` |
| `#status` | Show system status | `#status` |
| `#welcome` | Show welcome message | `#welcome` |
| `#clear` | Clear screen | `#clear` |
| `printall` | Show all variables | `printall` |
| `evalall` | Execute lazy evaluations | `evalall` |
| `exit` | Exit REPL | `exit` |

## Quick Setup

### Ubuntu/Debian
```bash
# Install dependencies
sudo apt install build-essential clang cmake pkg-config \
    libreadline-dev libtbb-dev libnotify-dev libgtest-dev

# Clone and build
git clone --recursive https://github.com/Fabio3rs/my-cpp-repl-study.git
cd my-cpp-repl-study
mkdir build && cd build
cmake .. && make -j$(nproc)

# Run
./cpprepl
```

## Example Sessions

### Basic Usage
```cpp
>>> int x = 42;
Compiled successfully (93ms)

>>> std::vector<int> v = {1, 2, 3};
Compiled successfully (89ms)

>>> #return v.size()
3
```

### Library Integration
```cpp
>>> #includedir /usr/include/eigen3
>>> #lib blas
>>> #include <Eigen/Dense>
>>> Eigen::MatrixXd m(2,2);
Compiled successfully (234ms)
```

### Error Recovery (with -s flag)
```cpp
>>> int* ptr = nullptr;
>>> *ptr = 42;  // Crash
WARNING: SEGV at: 0x0
Recovered - continue coding

>>> int safe_value = 42;  // Works normally
Compiled successfully (45ms)
```

## Performance Tips

- **Use caching:** Identical code reuses compiled libraries (~1-15μs)
- **Batch operations:** Group related declarations together  
- **Preload libraries:** Use `#includedir` and `#lib` at session start
- **Enable parallel builds:** Automatic for multi-source operations

## Troubleshooting

### Common Build Issues
```bash
# Missing submodules
git submodule update --init --recursive

# Missing dependencies
sudo apt install libreadline-dev libtbb-dev libnotify-dev

# Clang not found
sudo apt install clang libclang-dev

# Test failures
ctest --output-on-failure
```

### Runtime Issues
```bash
# Enable verbose output
./cpprepl -vv

# Enable crash protection  
./cpprepl -s

# Check system status
>>> #status
```

---

*For complete documentation, see [USER_GUIDE.md](USER_GUIDE.md) and [DEVELOPER.md](DEVELOPER.md)*