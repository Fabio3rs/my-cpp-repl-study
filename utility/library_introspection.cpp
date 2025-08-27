#include "../include/utility/library_introspection.hpp"
#include "../repl.hpp" // Para VarDecl

#include "file_raii.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <iostream>
#include <system_error>

namespace utility {

constexpr int MAX_LINE_LENGTH = 1024;

auto getBuiltFileDecls(const std::string &path) -> std::vector<VarDecl> {
    std::vector<VarDecl> vars;
    char line[MAX_LINE_LENGTH]{};

    // Get the address of the symbol within the library using nm
    auto command = std::format("nm -D --defined-only {} | grep ' T '", path);
    auto symbol_address_command = utility::make_popen(command, "r");
    if (!symbol_address_command) {
        perror("popen");
        exit(EXIT_FAILURE);
    }

    // Parse the output of nm command to get the symbol address
    while (fgets(line, MAX_LINE_LENGTH, symbol_address_command.get()) !=
               nullptr &&
           !std::feof(symbol_address_command.get())) {
        char address[32]{};
        char symbol_location[256]{};
        char symbol_name[512]{};
        sscanf(line, "%16s %s %s", address, symbol_location, symbol_name);
        vars.push_back({.name = symbol_name,
                        .mangledName = symbol_name,
                        .kind = "FunctionDecl"});
    }

    return vars;
}

auto getLibraryStartAddress(const char *library_name) -> uintptr_t {
    char line[MAX_LINE_LENGTH]{};
    char library_path[MAX_LINE_LENGTH]{};
    uintptr_t start_address{}, end_address{};

    // Open /proc/self/maps
    auto maps_file = make_fopen("/proc/self/maps", "r");
    if (!maps_file) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    // Search for the library in memory maps
    while (fgets(line, MAX_LINE_LENGTH, maps_file.get()) != nullptr) {
        library_path[0] = '\0';
        line[strcspn(line, "\n")] = '\0';
        std::cout << strlen(line) << "   \"" << line << '"' << std::endl;
        try {
            sscanf(line, "%zx-%zx %*s %*s %*s %*s %1023s", &start_address,
                   &end_address, library_path);
            std::error_code ec;
            if (std::filesystem::equivalent(library_path, library_name, ec) &&
                !ec) {
                break;
            }
        } catch (...) {
        }
    }

    if (library_path[0] == '\0') {
        std::cout << std::format("Library {} not found in memory maps.\n",
                                 library_name);
        return 0;
    }

    return start_address;
}

auto getSymbolAddress(const char *library_name, const char *symbol_name)
    -> uintptr_t {
    char line[MAX_LINE_LENGTH]{};
    char library_path[MAX_LINE_LENGTH]{};
    uintptr_t start_address{}, end_address{};
    uintptr_t symbol_offset = 0;

    // Open /proc/self/maps
    auto maps_file = make_fopen("/proc/self/maps", "r");
    if (!maps_file) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    // Search for the library in memory maps
    while (fgets(line, MAX_LINE_LENGTH, maps_file.get()) != nullptr) {
        library_path[0] = '\0';
        int readed = sscanf(line, "%zx-%zx %*s %*s %*s %*s %s", &start_address,
                            &end_address, library_path);
        std::cout << readed << "   " << library_path << "   " << library_name
                  << std::endl;
        if (strnlen(library_path, std::size(library_path)) == 0) {
            continue;
        }

        if (std::filesystem::equivalent(library_path, library_name)) {
            // Get the base address of the library
            break;
        }
    }

    if (strlen(library_path) == 0) {
        std::cout << std::format("Library {} not found in memory maps.\n",
                                 library_name);
        return 0;
    }

    // Get the address of the symbol within the library using nm
    auto command = std::format("nm -D --defined-only {} | grep ' {}$'",
                               library_path, symbol_name);
    auto symbol_address_command = make_popen(command, "r");
    if (!symbol_address_command) {
        perror("popen");
        exit(EXIT_FAILURE);
    }

    // Parse the output of nm command to get the symbol address
    if (fgets(line, MAX_LINE_LENGTH, symbol_address_command.get()) != nullptr) {
        char address[17]; // Assuming address length of 16 characters
        sscanf(line, "%16s", address);
        sscanf(address, "%zx",
               &symbol_offset); // Convert hex string to unsigned long
        std::cout << std::format("Address of symbol {} in {}: 0x{:x}\n",
                                 symbol_name, library_name,
                                 start_address + symbol_offset);
    } else {
        std::cout << std::format("Symbol {} not found in {}.\n", symbol_name,
                                 library_name);
    }

    return start_address + symbol_offset;
}

} // namespace utility
