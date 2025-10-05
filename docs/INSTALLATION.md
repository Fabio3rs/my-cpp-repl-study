# C++ REPL - Installation Guide

## System Requirements

### Supported Platforms
- **Linux** (Ubuntu 20.04+, Debian 11+, other POSIX-compliant systems)
- **Architecture:** x86_64, aarch64

### Required Dependencies

**Core Build Tools:**
- **Clang/LLVM** 18+ (recommended; C++20 support and tested with Clang 18)
- **CMake** 3.10+
- **GNU Make** or **Ninja** build system
- **pkg-config** for dependency management
- **Git** with submodule support

**System Libraries:**
- **libreadline-dev** - Command line editing and history
- **libtbb-dev** - Intel Threading Building Blocks for parallel processing
- **libnotify-dev** - Desktop notification support

**Optional Dependencies:**
- **libgtest-dev** - GoogleTest framework for testing
- **nlohmann-json-dev** - JSON support for LSP completion demos
- **doxygen** - API documentation generation
- **graphviz** - Documentation diagrams

## Installation Methods

### Method 1: Package Manager Installation (Ubuntu/Debian)

```bash
# Update package lists
sudo apt update

# Install all required dependencies
sudo apt install \
    build-essential \
    clang \
    cmake \
    pkg-config \
    libreadline-dev \
    libtbb-dev \
    libnotify-dev \
    libgtest-dev \
    ninja-build \
    git

# Optional dependencies
sudo apt install \
    nlohmann-json3-dev \
    doxygen \
    graphviz
```

### Method 2: Manual Dependency Build

For systems without package managers or newer dependency versions:

**Clang/LLVM:**
```bash
# Download and build LLVM/Clang 18+ (example uses a published release)
wget https://github.com/llvm/llvm-project/releases/download/llvmorg-18.0.0/llvm-project-18.0.0.src.tar.xz
tar -xf llvm-project-18.0.0.src.tar.xz
cd llvm-project-18.0.0.src
mkdir build && cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_ENABLE_PROJECTS="clang" \
  -DCMAKE_INSTALL_PREFIX=/usr/local \
  ../llvm
ninja install
```

**Intel TBB:**
```bash
git clone https://github.com/oneapi-src/oneTBB.git
cd oneTBB
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr/local ..
make -j$(nproc) install
```

## Build Process

### Step 1: Clone Repository

```bash
# Clone with all submodules
git clone --recursive https://github.com/Fabio3rs/my-cpp-repl-study.git
cd my-cpp-repl-study

# If already cloned without --recursive
git submodule update --init --recursive
```

**Verify Submodules:**
```bash
git submodule status
# Should show:
#  -7a608d985f832d535f941fdbd4fe4885cba92036 editor
#  -e25519a8acdc312e681742a2b23d22f26473f4aa ninja  
#  -52d05597d2b0b0be30e6dc5f46282fad798c61dc segvcatch
#  -6952f8dfce42d27bd2eca4f0b4260c06c1e7efef simdjson
```

### Step 2: Configure Build

```bash
mkdir build && cd build

# Basic configuration
cmake ..

# Advanced configuration
cmake .. \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_NOTIFICATIONS=ON \
  -DENABLE_ICONS=ON \
  -DENABLE_SANITIZERS=OFF
```

**Build Options:**

| Option | Default | Description |
|--------|---------|-------------|
| `CMAKE_BUILD_TYPE` | `Release` | Build type: Release/Debug/RelWithDebInfo |
| `ENABLE_NOTIFICATIONS` | `ON` | Desktop notification support |
| `ENABLE_ICONS` | `ON` | Icon support (requires wget, optionally inkscape) |
| `ENABLE_SANITIZERS` | `OFF` | Address and UB sanitizers (debug builds only) |
| `CMAKE_CXX_STANDARD` | `20` | C++ standard version |

### Step 3: Build

```bash
# Using Make (default)
make -j$(nproc)

# Using Ninja (if available)
ninja

# Check build artifacts
ls -la cpprepl
```

**Expected Build Time:**
- **Clean build:** ~2-5 minutes (depends on system)
- **Incremental build:** ~10-30 seconds
- **Test build:** +1-2 minutes

### Step 4: Test Installation

```bash
# Run unit tests
ctest --output-on-failure

# Quick functionality test
echo "int x = 42; #return x" | ./cpprepl -r /dev/stdin

# Interactive test
./cpprepl
>>> int test = 123;
>>> #return test
123
>>> exit
```

## Development Build

### Debug Build with Sanitizers

```bash
mkdir debug_build && cd debug_build
cmake .. \
  -DCMAKE_BUILD_TYPE=Debug \
  -DENABLE_SANITIZERS=ON \
  -DCMAKE_CXX_FLAGS="-g -O0"
make -j$(nproc)

# Test with sanitizers
./cpprepl -s -vv
```

### Custom Clang Version

If you have multiple Clang versions installed, prefer Clang 18 or newer. Example (adjust to your installed versions):

```bash
# Use specific Clang version
export CC=clang-18
export CXX=clang++-18
cmake .. -DCMAKE_CXX_COMPILER=clang++-18
make -j$(nproc)
```

## Troubleshooting Installation

### Common Issues

**1. Submodule Initialization Failure**
```bash
# Error: "fatal: No url found for submodule"
# Solution:
git submodule sync
git submodule update --init --recursive
```

**2. Missing Dependencies**
```bash
# Error: "Could not find TBB"
# Solution:
sudo apt install libtbb-dev
# Or build from source (see Manual Dependency Build)
```

**3. Clang Version Issues**
```bash
# Error: "Clang version too old"
# Solution: install Clang 18+ and point CMake to the newer compiler
sudo apt install clang-18 libclang-18-dev
export CXX=clang++-18
cmake .. -DCMAKE_CXX_COMPILER=clang++-18
```

**4. CMake Version Issues**
```bash
# Error: "CMake 3.10 or higher is required"
# Solution (Ubuntu 18.04):
sudo snap install cmake --classic
# Or build from source
```

**5. Build Permissions**
```bash
# Error: "Permission denied" during build
# Solution:
chmod +x format-code.sh
# Ensure user has write permissions in build directory
```

### Verification

**Successful Installation Checklist:**
- [ ] All submodules initialized (`git submodule status`)
- [ ] CMake configuration successful (no missing dependencies)
- [ ] Build completes without errors
- [ ] Tests pass (`ctest --output-on-failure`)
- [ ] Basic REPL functionality works (`echo "int x=42; #return x" | ./cpprepl -r /dev/stdin`)
- [ ] Signal handling works (if using `-s` flag)

**Performance Verification:**
```bash
# Should complete in ~93ms average
time echo "int x = 42; #return x" | ./cpprepl -r /dev/stdin

# Memory usage check (should be ~8-12MB)
valgrind --tool=massif ./cpprepl -r /dev/stdin < simple_test.repl
```

## Advanced Installation

### Custom Installation Prefix

```bash
cmake .. -DCMAKE_INSTALL_PREFIX=/opt/cpprepl
make install

# Add to PATH
echo 'export PATH="/opt/cpprepl/bin:$PATH"' >> ~/.bashrc
```

### Static Linking

```bash
cmake .. \
  -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_EXE_LINKER_FLAGS="-static"
make -j$(nproc)

# Verify static linking
ldd cpprepl  # Should show minimal dynamic dependencies
```

### Cross-Compilation

```bash
# For aarch64 (if supported)
cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=../cmake/aarch64-linux-gnu.cmake \
  -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-clang++
make -j$(nproc)
```

## Post-Installation

### Desktop Integration

```bash
# Create desktop entry (optional)
cat > ~/.local/share/applications/cpprepl.desktop << EOF
[Desktop Entry]
Name=C++ REPL
Comment=Interactive C++ Development Environment
Exec=/path/to/cpprepl
Icon=utilities-terminal
Terminal=true
Type=Application
Categories=Development;IDE;
EOF
```

### Shell Integration

```bash
# Add aliases to ~/.bashrc
echo 'alias cpprepl="cpprepl -s"' >> ~/.bashrc  # Always use safe mode
echo 'alias cppdev="cpprepl -sv"' >> ~/.bashrc  # Development mode
```

### Documentation Generation

```bash
# Generate API documentation
doxygen Doxyfile

# View documentation
firefox docs/api/html/index.html
```

---

## Uninstallation

```bash
# Remove build directory
rm -rf build/

# Remove installed files (if using CMAKE_INSTALL_PREFIX)
sudo rm -rf /opt/cpprepl  # or wherever installed

# Remove user configuration (optional)
rm -rf ~/.config/cpprepl  # if any config files created
```

---

*For usage instructions, see [USER_GUIDE.md](USER_GUIDE.md). For development setup, see [DEVELOPER.md](DEVELOPER.md).*
