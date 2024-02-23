cmake_minimum_required(VERSION 3.10)

project(cpprepl)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS ON)

include(CTest)
enable_testing()
find_package(GTest)

# export commands
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_link_options(-Wl,--export-dynamic)

add_library(cpprepl_lib repl.cpp simdjson.cpp simdjson.h)
target_precompile_headers(cpprepl_lib PUBLIC stdafx.hpp)
target_link_libraries(cpprepl_lib pthread dl tbb)
add_executable(cpprepl main.cpp)
target_precompile_headers(cpprepl PUBLIC stdafx.hpp)
target_link_libraries(cpprepl cpprepl_lib)

# add_executable(libclangtest libclangtest.cpp)
# target_include_directories(libclangtest PRIVATE /usr/lib/llvm-15/include)
# target_link_directories(libclangtest PRIVATE /usr/lib/llvm-15/lib)
# target_link_libraries(libclangtest cpprepl_lib clang)

# add_executable(pistachetest pistache/main.cpp)
# target_link_libraries(pistachetest cpprepl_lib pistache)

add_subdirectory(tests)
