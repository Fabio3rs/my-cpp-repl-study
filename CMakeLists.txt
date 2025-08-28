cmake_minimum_required(VERSION 3.10)

project(cpprepl)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS ON)

set(CMAKE_FIND_PACKAGE_SORT_ORDER NATURAL)   # compara números corretamente (1.10 > 1.9)
set(CMAKE_FIND_PACKAGE_SORT_DIRECTION DEC)   # diretórios de versão maior primeiro

include(CTest)
enable_testing()
find_package(GTest)

include_directories(SYSTEM ${CMAKE_CURRENT_SOURCE_DIR}/segvcatch/lib)
include_directories(${CMAKE_SOURCE_DIR}/include)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/segvcatch/lib)
#add_compile_options(-fno-omit-frame-pointer -fsanitize=address -fsanitize=alignment -fsanitize=bool -fsanitize=bounds -fsanitize=enum -fsanitize=float-cast-overflow -fsanitize=float-divide-by-zero -fsanitize=integer-divide-by-zero -fsanitize=leak -fsanitize=nonnull-attribute -fsanitize=pointer-compare -fsanitize=pointer-overflow -fsanitize=pointer-subtract -fsanitize=return -fsanitize=returns-nonnull-attribute -fsanitize=shift -fsanitize=signed-integer-overflow -fsanitize=undefined -fsanitize=unreachable -fsanitize=vla-bound -fsanitize=vptr -g)
#add_link_options(-fno-omit-frame-pointer -fsanitize=address -fsanitize=alignment -fsanitize=bool -fsanitize=bounds -fsanitize=enum -fsanitize=float-cast-overflow -fsanitize=float-divide-by-zero -fsanitize=integer-divide-by-zero -fsanitize=leak -fsanitize=nonnull-attribute -fsanitize=pointer-compare -fsanitize=pointer-overflow -fsanitize=pointer-subtract -fsanitize=return -fsanitize=returns-nonnull-attribute -fsanitize=shift -fsanitize=signed-integer-overflow -fsanitize=undefined -fsanitize=unreachable -fsanitize=vla-bound -fsanitize=vptr -g)

include(CPack)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBNOTIFY REQUIRED IMPORTED_TARGET libnotify)
find_package(TBB REQUIRED)

pkg_check_modules(READLINE REQUIRED readline)

# Check for libclang availability
find_package(Clang QUIET)

if (Clang_FOUND)
    message(STATUS "Clang found: ${Clang_VERSION}")
    message(STATUS "Clang include dirs: ${CLANG_INCLUDE_DIRS}")
    message(STATUS "Clang libraries: ${CLANG_LIBRARIES}")
else()
    message(STATUS "Clang not found")
endif()

include_directories(SYSTEM ${TBB_INCLUDE_DIRS})

option(ENABLE_NOTIFICATIONS "Enable desktop notifications" ON)

# export commands
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_link_options(-Wl,--export-dynamic)

# Base sources for cpprepl_lib
set(CPPREPL_LIB_SOURCES
    repl.cpp
    simdjson.cpp
    simdjson.h
    ast_context.cpp
    clang_ast_adapter.cpp
    src/compiler/compiler_service.cpp
    utility/backtraced_exceptions.cpp
    utility/library_introspection.cpp
    printerOverloads.cpp
    utility/quote.cpp
    src/execution/execution_engine.cpp
    src/execution/symbol_resolver.cpp
)

# Conditionally add completion sources if libclang is available
if(Clang_FOUND)
    message(STATUS "Clang found: enabling context-aware completion")
    list(APPEND CPPREPL_LIB_SOURCES
        src/completion/clang_completion.cpp
        src/completion/readline_integration.cpp
    )
else()
    message(STATUS "Clang not found: completion features disabled")
endif()

add_library(cpprepl_lib ${CPPREPL_LIB_SOURCES})
target_precompile_headers(cpprepl_lib PUBLIC stdafx.hpp)
target_link_libraries(cpprepl_lib pthread dl tbb readline)

# Link libclang if available
if(Clang_FOUND)
    target_link_libraries(cpprepl_lib clangAST clangBasic clangFrontend clangSerialization clangTooling)
    target_include_directories(cpprepl_lib PUBLIC SYSTEM ${CLANG_INCLUDE_DIRS})
    target_compile_definitions(cpprepl_lib PUBLIC CLANG_COMPLETION_ENABLED)
endif()

if (ENABLE_NOTIFICATIONS)
    target_link_libraries(cpprepl_lib PkgConfig::LIBNOTIFY)
    target_compile_definitions(cpprepl_lib PUBLIC ENABLE_NOTIFICATIONS)
endif()

add_executable(cpprepl main.cpp)
target_precompile_headers(cpprepl PUBLIC stdafx.hpp)
target_link_libraries(cpprepl cpprepl_lib tbb segvcatch)

# Demo executable for completion system (always available)
add_executable(completion_demo
    examples/completion_demo.cpp
    src/completion/clang_completion.cpp
    src/completion/readline_integration.cpp
)
target_include_directories(completion_demo PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(completion_demo readline)
target_compile_features(completion_demo PRIVATE cxx_std_20)

# If libclang is available, enable it for the demo too
if(Clang_FOUND)
    target_link_libraries(completion_demo clangAST clangBasic clangFrontend clangSerialization clangTooling)
    target_include_directories(completion_demo PRIVATE ${CLANG_INCLUDE_DIRS})
    target_compile_definitions(completion_demo PUBLIC CLANG_COMPLETION_ENABLED)
endif()

#add_executable(libclangtest libclangtest.cpp)
#target_include_directories(libclangtest PRIVATE /usr/lib/llvm-15/include)
#target_link_directories(libclangtest PRIVATE /usr/lib/llvm-15/lib)
#target_link_libraries(libclangtest cpprepl_lib clang)

# add_executable(pistachetest pistache/main.cpp)
# target_link_libraries(pistachetest cpprepl_lib pistache)

add_subdirectory(tests)
