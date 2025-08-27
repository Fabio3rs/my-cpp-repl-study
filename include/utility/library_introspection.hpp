#pragma once

#include <string>
#include <vector>

struct VarDecl;

namespace utility {

/**
 * @brief Extracts function declarations from a built library using nm command
 * @param path Path to the library file
 * @return Vector of VarDecl representing the function symbols found
 */
auto getBuiltFileDecls(const std::string &path) -> std::vector<VarDecl>;

} // namespace utility
