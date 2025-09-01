#pragma once
#include "file_raii.hpp"
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

namespace utility {

inline auto
runProgramGetOutput(std::string_view cmd) -> std::pair<std::string, int> {
    std::pair<std::string, int> result{{}, -1};

    auto pipe = utility::make_popen(cmd.data(), "r");

    if (!pipe) {
        std::cerr << "popen() failed!" << std::endl;
        return result;
    }

    constexpr size_t MBPAGE2 = 1024 * 2;

    auto &buffer = result.first;
    try {
        buffer.resize(MBPAGE2);
    } catch (const std::bad_alloc &e) {
        pipe.reset();
        std::cerr << "bad_alloc caught: " << e.what() << std::endl;
        return result;
    }

    size_t finalSize = 0;

    while (!feof(pipe.get())) {
        size_t read = fread(buffer.data() + finalSize, 1,
                            buffer.size() - finalSize, pipe.get());

        if (read < 0) {
            std::cerr << "fread() failed!" << std::endl;
            break;
        }

        finalSize += read;

        try {
            if (finalSize == buffer.size()) {
                buffer.resize(buffer.size() * 2);
            }
        } catch (const std::bad_alloc &e) {
            std::cerr << "bad_alloc caught: " << e.what() << std::endl;
            break;
        }
    }

    pipe.reset();
    result.second = pipe.get_deleter().retcode;

    if (result.second != 0) {
        std::cerr << std::format("program failed! {}\n", result.second);
    }

    buffer.resize(finalSize);

    return result;
}

} // namespace utility
