#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct VarDecl;

namespace utility {

struct SymbolDef {
    std::string nativeName; // mangled (nm sem -C)
    uintptr_t address{};    // st_value (offset no DSO)
    char libSection{};      // 'T','t','W','w','D','d','B','b','R','r',...
};

/**
 * @brief Extracts function declarations from a built library using nm command
 * @param path Path to the library file
 * @return Vector of VarDecl representing the function symbols found
 */
auto getBuiltFileDecls(const std::string &path) -> std::vector<VarDecl>;

auto getAllBuiltFileDecls(const std::string &path) -> std::vector<SymbolDef>;

/**
 * @brief Gets the start address of a library in memory
 * @param library_name Path to the library file
 * @return Start address of the library in memory, or 0 if not found
 */
auto getLibraryStartAddress(const char *library_name) -> uintptr_t;

/**
 * @brief Gets the address of a symbol within a library
 * @param library_name Path to the library file
 * @param symbol_name Name of the symbol to find
 * @return Address of the symbol in memory, or 0 if not found
 */
auto getSymbolAddress(const char *library_name,
                      const char *symbol_name) -> uintptr_t;

} // namespace utility
