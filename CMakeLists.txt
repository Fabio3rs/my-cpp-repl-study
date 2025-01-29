cmake_minimum_required(VERSION 3.10)

project(cpprepl)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS ON)

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

include_directories(SYSTEM ${TBB_INCLUDE_DIRS})

option(ENABLE_NOTIFICATIONS "Enable desktop notifications" ON)

# export commands
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_link_options(-Wl,--export-dynamic)

add_library(cpprepl_lib repl.cpp simdjson.cpp simdjson.h utility/backtraced_exceptions.cpp printerOverloads.cpp utility/quote.cpp)
target_precompile_headers(cpprepl_lib PUBLIC stdafx.hpp)
target_link_libraries(cpprepl_lib pthread dl tbb readline)

if (ENABLE_NOTIFICATIONS)
    target_link_libraries(cpprepl_lib PkgConfig::LIBNOTIFY)
    target_compile_definitions(cpprepl_lib PUBLIC ENABLE_NOTIFICATIONS)
endif()

add_executable(cpprepl main.cpp)
target_precompile_headers(cpprepl PUBLIC stdafx.hpp)
target_link_libraries(cpprepl cpprepl_lib tbb segvcatch)

#add_executable(libclangtest libclangtest.cpp)
#target_include_directories(libclangtest PRIVATE /usr/lib/llvm-15/include)
#target_link_directories(libclangtest PRIVATE /usr/lib/llvm-15/lib)
#target_link_libraries(libclangtest cpprepl_lib clang)

# add_executable(pistachetest pistache/main.cpp)
# target_link_libraries(pistachetest cpprepl_lib pistache)

add_subdirectory(tests)
