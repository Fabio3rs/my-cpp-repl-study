# ==============================================================================
# CPPREPL - Modern C++20/23 REPL with Modular Architecture
# ==============================================================================
cmake_minimum_required(VERSION 3.10)
project(cpprepl VERSION 1.5.0)

# ==============================================================================
# PROJECT CONFIGURATION
# ==============================================================================

# C++ Standard Configuration (C++20/23 as per Core Guidelines)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS ON)  # GNU extensions allowed but not mandatory

# CMake package finding configuration
set(CMAKE_FIND_PACKAGE_SORT_ORDER NATURAL)   # compara números corretamente (1.10 > 1.9)
set(CMAKE_FIND_PACKAGE_SORT_DIRECTION DEC)   # diretórios de versão maior primeiro

# Compile commands export for IDE support and clang-tidy
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ==============================================================================
# BUILD OPTIONS
# ==============================================================================
option(ENABLE_NOTIFICATIONS "Enable desktop notifications" ON)
option(ENABLE_ICONS "Enable icon support (requires wget and optionally inkscape)" ON)
option(ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers (debug builds)" OFF)

# ==============================================================================
# TESTING SETUP
# ==============================================================================
include(CTest)
enable_testing()
find_package(GTest)

# ==============================================================================
# DEPENDENCY DISCOVERY
# ==============================================================================

# Required dependencies
find_package(PkgConfig REQUIRED)
find_package(TBB REQUIRED)
pkg_check_modules(READLINE REQUIRED readline)
pkg_check_modules(LIBNOTIFY REQUIRED IMPORTED_TARGET libnotify)

# Optional dependencies
find_package(nlohmann_json QUIET)
find_package(Clang QUIET)

# Optional dependencies
find_package(nlohmann_json QUIET)
find_package(Clang QUIET)

# ==============================================================================
# DEPENDENCY STATUS REPORTING
# ==============================================================================

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-fxray-instrument -fxray-instruction-threshold=1 -g)
  add_link_options(-fxray-instrument)  # puxa o runtime do XRay automaticamente
endif()

# Report Clang availability and configuration
if (Clang_FOUND)
    message(STATUS "✓ Clang found: ${Clang_VERSION}")
    message(STATUS "  - Include dirs: ${CLANG_INCLUDE_DIRS}")
    message(STATUS "  - Libraries: ${CLANG_LIBRARIES}")
    message(STATUS "  - Semantic completion will be enabled")
else()
    message(STATUS "✗ Clang not found - semantic completion will use mock implementation")
endif()

# Report nlohmann_json availability
if(nlohmann_json_FOUND)
    message(STATUS "✓ nlohmann_json found - LSP completion demo enabled")
else()
    message(STATUS "✗ nlohmann_json not found - LSP completion demo disabled")
endif()

# Report GTest availability
if(GTest_FOUND)
    message(STATUS "✓ GTest found - comprehensive testing enabled")
else()
    message(STATUS "✗ GTest not found - testing disabled")
endif()

# ==============================================================================
# COMPILER CONFIGURATION
# ==============================================================================

# Global include directories
include_directories(SYSTEM ${CMAKE_CURRENT_SOURCE_DIR}/segvcatch/lib)
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(SYSTEM ${TBB_INCLUDE_DIRS})

# POSIX-compliant symbol export for dynamic loading (required for REPL)
add_link_options(-Wl,--export-dynamic)

# Sanitizers configuration (debug builds only)
if(ENABLE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "✓ Sanitizers enabled for debug build")
    set(SANITIZER_FLAGS
        -fno-omit-frame-pointer
        -fsanitize=address
        -fsanitize=undefined
        -fsanitize=bounds
        -fsanitize=leak
        -g
    )
    add_compile_options(${SANITIZER_FLAGS})
    add_link_options(${SANITIZER_FLAGS})
else()
    message(STATUS "✗ Sanitizers disabled")
endif()

# ==============================================================================
# SUBMODULES AND EXTERNAL LIBRARIES
# ==============================================================================

# SEGV catch library (signal handling for REPL)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/segvcatch/lib)

# CPack for distribution
include(CPack)

# ==============================================================================
# CORE LIBRARY CONFIGURATION
# ==============================================================================

# Base sources for cpprepl_lib (modular architecture)
set(CPPREPL_LIB_SOURCES
    # Main REPL components
    repl.cpp
    ast_context.cpp
    clang_ast_adapter.cpp

    # Modular source components
    src/compiler/compiler_service.cpp
    src/execution/execution_engine.cpp
    src/execution/symbol_resolver.cpp
    src/completion/simple_readline_completion.cpp

    # Utility components
    utility/backtraced_exceptions.cpp
    utility/library_introspection.cpp
    utility/quote.cpp
    printerOverloads.cpp

    # External dependencies
    simdjson.cpp
    simdjson.h
)

# Conditional sources based on available dependencies
if(Clang_FOUND)
    message(STATUS "✓ Adding Clang-based completion sources")
    list(APPEND CPPREPL_LIB_SOURCES
        src/completion/clang_completion.cpp
        src/completion/readline_integration.cpp
    )
endif()

# Create the main library with modern CMake patterns
add_library(cpprepl_lib STATIC ${CPPREPL_LIB_SOURCES})

# Base library configuration
target_include_directories(cpprepl_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Core library dependencies (always required)
target_link_libraries(cpprepl_lib PUBLIC
    pthread
    dl
    tbb
    readline
)

# Precompiled headers for faster compilation
target_precompile_headers(cpprepl_lib PUBLIC stdafx.hpp)

# ==============================================================================
# CONDITIONAL LIBRARY FEATURES
# ==============================================================================

# Clang completion support
if(Clang_FOUND)
    target_include_directories(cpprepl_lib PUBLIC SYSTEM ${CLANG_INCLUDE_DIRS})
    target_compile_definitions(cpprepl_lib PUBLIC CLANG_COMPLETION_ENABLED)
    target_link_libraries(cpprepl_lib PUBLIC
        libclang
        clang
        clangAST
        clangBasic
        clangFrontend
        clangSerialization
        clangTooling
        ${LLVM_LIBRARIES}
    )
endif()

# Notification support
if(ENABLE_NOTIFICATIONS)
    message(STATUS "✓ Desktop notifications enabled")
    target_link_libraries(cpprepl_lib PUBLIC PkgConfig::LIBNOTIFY)
    target_compile_definitions(cpprepl_lib PUBLIC ENABLE_NOTIFICATIONS)
else()
    message(STATUS "✗ Desktop notifications disabled")
endif()

# ==============================================================================
# ICON SUPPORT CONFIGURATION
# ==============================================================================

if(ENABLE_ICONS)
    message(STATUS "✓ Icon support enabled")

    # Define icon paths
    set(CPPREPL_ICON_DIR "${CMAKE_SOURCE_DIR}/assets/icons")
    set(CPPREPL_ICON_PNG_DIR "${CMAKE_SOURCE_DIR}/assets/icons/png")

    # Download icons target
    add_custom_target(download-icons
        COMMAND ${CMAKE_SOURCE_DIR}/script/download_icons.sh
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Downloading Lucide icons..."
    )

    # Optional PNG conversion if Inkscape is available
    find_program(INKSCAPE_EXECUTABLE inkscape)
    if(INKSCAPE_EXECUTABLE)
        message(STATUS "  ✓ Inkscape found: ${INKSCAPE_EXECUTABLE} - PNG conversion available")

        add_custom_target(convert-icons
            COMMAND ${CMAKE_SOURCE_DIR}/script/convert_icons.sh ${CPPREPL_ICON_DIR} ${CPPREPL_ICON_PNG_DIR}
            DEPENDS download-icons
            COMMENT "Converting SVG icons to PNG format..."
        )

        add_dependencies(cpprepl_lib convert-icons)
        target_compile_definitions(cpprepl_lib PUBLIC CPPREPL_ICON_PNG_PATH="${CPPREPL_ICON_PNG_DIR}")
    else()
        message(STATUS "  ✗ Inkscape not found - only SVG icons available")
        add_dependencies(cpprepl_lib download-icons)
    endif()

    # Define icon path for the application
    target_compile_definitions(cpprepl_lib PUBLIC CPPREPL_ICON_PATH="${CPPREPL_ICON_DIR}")

else()
    message(STATUS "✗ Icon support disabled")
endif()

# ==============================================================================
# MAIN EXECUTABLE
# ==============================================================================

add_executable(cpprepl main.cpp)
target_precompile_headers(cpprepl PUBLIC stdafx.hpp)
target_link_libraries(cpprepl PRIVATE cpprepl_lib segvcatch)

# ==============================================================================
# DEMO EXECUTABLES AND EXAMPLES
# ==============================================================================

# ==============================================================================
# DEMO EXECUTABLES AND EXAMPLES
# ==============================================================================

# LSP completion demo (requires nlohmann_json)
if(nlohmann_json_FOUND)
    message(STATUS "✓ Building LSP completion demo with nlohmann_json")
    add_executable(lsp_completion_demo
        examples/lsp_completion_demo.cpp
        src/completion/lsp_readline_integration.cpp
    )

    target_link_libraries(lsp_completion_demo PRIVATE
        cpprepl_lib
        ${READLINE_LIBRARIES}
    )
    target_compile_definitions(lsp_completion_demo PUBLIC LSP_COMPLETION_ENABLED)
    target_include_directories(lsp_completion_demo PRIVATE
        src/completion
        include
    )
    target_include_directories(lsp_completion_demo SYSTEM PRIVATE
        ${nlohmann_json_INCLUDE_DIRS}
    )
else()
    message(STATUS "✗ nlohmann_json not found - LSP completion demo will not be built")
endif()

# LibClang completion demos (requires libclang)
if(Clang_FOUND)
    message(STATUS "✓ Building Clang completion demos")

    # Basic completion demo
    add_executable(completion_demo
        examples/completion_demo.cpp
    )
    target_link_libraries(completion_demo PRIVATE
        cpprepl_lib
        ${READLINE_LIBRARIES}
    )

    # Verbosity demo showing BuildSettings integration
    add_executable(verbosity_demo
        examples/verbosity_demo.cpp
    )
    target_link_libraries(verbosity_demo PRIVATE
        cpprepl_lib
        ${READLINE_LIBRARIES}
    )
else()
    message(STATUS "✗ Clang demos disabled - libclang not available")
endif()

# ==============================================================================
# TESTING SUBDIRECTORY
# ==============================================================================

add_subdirectory(tests)

# ==============================================================================
# BUILD SUMMARY
# ==============================================================================

message(STATUS "")
message(STATUS "=== CPPREPL v${PROJECT_VERSION} Build Configuration ===")
message(STATUS "Core Features:")
message(STATUS "  - C++${CMAKE_CXX_STANDARD} standard")
message(STATUS "  - Modular architecture (${CMAKE_CURRENT_LIST_DIR})")
message(STATUS "  - POSIX signal handling")
message(STATUS "")
message(STATUS "Optional Features:")
message(STATUS "  - Clang completion: ${Clang_FOUND}")
message(STATUS "  - nlohmann_json: ${nlohmann_json_FOUND}")
message(STATUS "  - Desktop notifications: ${ENABLE_NOTIFICATIONS}")
message(STATUS "  - Icon support: ${ENABLE_ICONS}")
message(STATUS "  - Testing (GTest): ${GTest_FOUND}")
message(STATUS "  - Sanitizers: ${ENABLE_SANITIZERS}")
message(STATUS "")
message(STATUS "Build targets available:")
message(STATUS "  - cpprepl (main executable)")
if(Clang_FOUND)
    message(STATUS "  - completion_demo, verbosity_demo")
endif()
if(nlohmann_json_FOUND)
    message(STATUS "  - lsp_completion_demo")
endif()
if(GTest_FOUND)
    message(STATUS "  - tests, compiler_tests, analysis_tests, utility_tests")
    if(Clang_FOUND)
        message(STATUS "  - completion_tests")
    endif()
endif()
message(STATUS "========================================")
message(STATUS "")
