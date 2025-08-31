#pragma once

#include <cstdlib>
#include <memory>
#include <string_view>

// Define a simple RAII wrapper for FILE* management
namespace utility {
struct FileCloser {
    void operator()(FILE *file) const {
        if (file) {
            std::fclose(file);
        }
    }
};

struct PopenCloser {
    int retcode = -1;
    void operator()(FILE *file) {
        if (file) {
            retcode = pclose(file);
        }
    }
};

using FileRAII = std::unique_ptr<FILE, FileCloser>;
using PopenRAII = std::unique_ptr<FILE, PopenCloser>;

inline auto make_popen(std::string_view command, const char *type)
    -> PopenRAII {
    return PopenRAII(popen(command.data(), type));
}

inline auto make_fopen(std::string_view filename, const char *mode)
    -> FileRAII {
    return FileRAII(fopen(filename.data(), mode));
}
} // namespace utility
