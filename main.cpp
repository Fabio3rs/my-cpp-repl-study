#include "stdafx.hpp"

#include "repl.hpp"

int main(int argc, char **argv) {
    initRepl();

    int c;
    while ((c = getopt(argc, argv, "r:")) != -1) {
        switch (c) {
        case 'r': {
            std::string_view replCmdsFile(optarg);

            std::fstream file(replCmdsFile.data(), std::ios::in);

            if (!file.is_open()) {
                std::cerr << "Cannot open file: " << replCmdsFile << '\n';
                return 1;
            }

            std::string line;
            while (std::getline(file, line)) {
                if (!extExecRepl(line)) {
                    break;
                }
            }
        } break;
        }
    }

    if (!bootstrapProgram) {
        repl();
    }

    try {
        if (bootstrapProgram) {
            return bootstrapProgram(argc, argv);
        }
    } catch (const std::exception &e) {
        std::cerr << "C++ exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
