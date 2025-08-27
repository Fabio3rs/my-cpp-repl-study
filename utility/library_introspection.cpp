#include "../include/utility/library_introspection.hpp"
#include "../repl.hpp" // Para VarDecl

#include "file_raii.hpp"
#include <cstdio>
#include <cstdlib>

namespace utility {

constexpr int MAX_LINE_LENGTH = 1024;

auto getBuiltFileDecls(const std::string &path) -> std::vector<VarDecl> {
    std::vector<VarDecl> vars;
    char command[2048]{}, line[MAX_LINE_LENGTH]{};

    // Get the address of the symbol within the library using nm
    snprintf(command, std::size(command), "nm %s | grep ' T '", path.c_str());
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

} // namespace utility
