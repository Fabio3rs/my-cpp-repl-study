cmake_minimum_required(VERSION 3.10)

project(cpprepl)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# export commands
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_link_options(-Wl,--export-dynamic)

add_executable(cpprepl main.cpp simdjson.cpp simdjson.h)


