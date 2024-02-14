#include <chrono>
#include <cstddef>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <sys/inotify.h>
#include <thread>
#include <unistd.h>

auto extExecRepl(std::string_view lineview) -> bool;

static constexpr size_t INOTIFY_EVENT_SIZE = (sizeof(struct inotify_event));
static constexpr size_t MAX_BUF_LEN = (1024 * (INOTIFY_EVENT_SIZE + 16));

static std::string_view trimStrview(std::string_view str) {
    str = str.substr(str.find_first_not_of(" \t\n\v\f\r\0"));
    str = str.substr(0, str.find_last_not_of(" \t\n\v\f\r\0") + 1);

    return str;
}

void rebuildUpdatedFile(std::chrono::steady_clock::time_point &last_time,
                        const std::chrono::steady_clock::time_point now,
                        const struct inotify_event *event,
                        const std::filesystem::path &filename) {
    if ((event->mask & IN_MODIFY) == 0) {
        return;
    }

    if ((now - last_time) < std::chrono::seconds(1)) {
        return;
    }

    if (std::filesystem::file_size(filename) == 0) {
        return;
    }

    std::cout << "File modified: " << filename << " rebuilt ver" << std::endl;

    last_time = now;
    auto cmd = "#eval " + filename.string();

    extExecRepl(cmd);
}

void loopBytesInotifyRebuildFiles(
    std::string_view file_to_watch, const char *buffer,
    std::chrono::steady_clock::time_point &last_time, int num_bytes) {
    auto now = std::chrono::steady_clock::now();
    std::filesystem::path path(file_to_watch);

    for (int i = 0; i < num_bytes; /**/) {
        const auto *event =
            reinterpret_cast<const struct inotify_event *>(&buffer[i]);

        std::string_view filename(event->name, strnlen(event->name, event->len));

        filename = trimStrview(filename);

        std::filesystem::path filename_path;

        if (filename.empty()) {
            filename = file_to_watch;
        } else {
            filename_path = path / filename;
        }

        std::cout << "Event: " << event->mask << "  " << filename << std::endl;

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        try {
            rebuildUpdatedFile(last_time, now, event, filename_path);
        } catch (const std::exception &e) {
            std::cerr << e.what() << std::endl;
        }

        i += INOTIFY_EVENT_SIZE + event->len;
    }
}

int monitorAndRebuildFileOrDirectory(std::string_view file_to_watch) {
    int fd, wd;
    char buffer[MAX_BUF_LEN];

    file_to_watch = trimStrview(file_to_watch);

    // Initialize inotify
    fd = inotify_init();
    if (fd < 0) {
        perror("inotify_init");
        return 1;
    }

    // Watch for write events on a specific file
    wd = inotify_add_watch(fd, file_to_watch.data(), IN_MODIFY);
    if (wd < 0) {
        perror("inotify_add_watch");
        return 1;
    }

    std::cout << "Watching file/directory: " << file_to_watch
              << " for write events..." << std::endl;

    auto last_time = std::chrono::steady_clock::now();

    while (true) {
        int num_bytes = read(fd, buffer, MAX_BUF_LEN);
        if (num_bytes < 0) {
            perror("read");
            return 1;
        }

        loopBytesInotifyRebuildFiles(file_to_watch, buffer, last_time,
                                     num_bytes);
    }

    // Clean up
    close(fd);
    return 0;
}
