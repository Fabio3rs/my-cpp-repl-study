#include "../stdafx.hpp"
#include <bits/getopt_core.h>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <dlfcn.h>
#include <exception>
#include <filesystem>
#include <iostream>
#include <libnotify/notification.h>
#include <libnotify/notify.h>
#include <string>
#include <string_view>
#include <sys/inotify.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#include "/mnt/projects/Projects/cpprepl/repl.hpp"

std::vector<std::string> commands;

template <class StrType = std::string>
static auto split(std::string_view strview, std::string_view term)
    -> std::vector<StrType> {
    size_t current = 0;
    std::vector<StrType> result;

    if (strview.empty()) {
        return result;
    }

    if (term.empty()) {
        result.emplace_back(strview);
        return result;
    }

    do {
        auto sep = strview.find(term, current);

        result.emplace_back(strview.substr(
            current, (sep == std::string_view::npos) ? sep : (sep - current)));

        if (sep == std::string_view::npos) {
            break;
        }

        current = sep + term.size();
    } while (current < strview.size());

    return result;
}

void ninjarebuild(bool doClean = true) {
    auto *ninja = dlopen("./libninjashared.so", RTLD_LAZY);

    if (ninja == nullptr) {
        std::cerr << "Error: " << dlerror() << std::endl;
        return;
    }

    auto *ninja_exec =
        (int (*)(int argc, char **argv))dlsym(ninja, "libninja_main");

    if (ninja_exec == nullptr) {
        std::cerr << "Error: " << dlerror() << std::endl;
        return;
    }

    auto comandHookPtr = dlsym(ninja, "runCommandHook");
    if (comandHookPtr == nullptr) {
        std::cerr << "Error: " << dlerror() << std::endl;
        return;
    }

    auto &runCommandHook =
        *(void (**)(const char *command, size_t size))comandHookPtr;

    runCommandHook = [](const char *command, size_t size) {
        commands.emplace_back(command, size);
    };

    char name[] = {"ninjadev"};
    char frstarg[] = "-t";
    char cleanrg[] = "clean";
    char allrg[] = "all";
    char *argv[] = {name, frstarg, cleanrg, allrg, nullptr};
    int argc = 4;

    if (doClean) {
        optind = 0;
        opterr = 0;
        optopt = 0;

        int ninjares = ninja_exec(argc, argv);

        if (ninjares != 0) {
            std::cerr << "Ninja run error" << std::endl;
            return;
        }
    }

    argc = 1;

    optind = 0;
    opterr = 0;
    optopt = 0;

    ninja_exec(argc, argv);

    std::vector<std::string> objects;
    std::unordered_map<std::string, std::string> filescmds;

    std::vector<std::string> libs;

    for (auto &command : commands) {
        if (command.find("clang++") > 30) {
            continue;
        }

        auto parts = split<std::string>(command, " ");

        bool isObject = command.find(" -c") != std::string::npos;
        bool isOutput = false;
        bool isInclude = false;
        bool isSystemInclude = false;

        for (const auto &part : parts) {
            if (isOutput && isObject) {
                objects.emplace_back(part);
                isOutput = false;

                std::cout << "\n\n\n\n" << command << "\n\n\n\n" << std::endl;
                continue;
            }

            if (isInclude) {
                addIncludeDirectory(part);
                isSystemInclude = false;
                isInclude = false;
            }

            if (part.find(".so") != std::string::npos) {
                if (std::filesystem::exists(part)) {
                    libs.emplace_back(part);
                }
            }

            if (part.ends_with(".cpp")) {
                filescmds[part] = command;
                continue;
            }

            if (part == "-o") {
                isOutput = true;
            }

            if (part.starts_with("-I")) {
                if (part.size() == std::string_view("-I").size()) {
                    isInclude = true;
                } else {
                    addIncludeDirectory(part.substr(2));
                }
            }

            if (part.starts_with("-isystem")) {
                if (part.size() == std::string_view("-isystem").size()) {
                    isInclude = true;
                    isSystemInclude = true;
                } else {
                    addIncludeDirectory(part.substr(8));
                }
            }
        }
    }

    for (const auto &lib : libs) {
        std::cout << "Lib: " << lib << std::endl;

        /*void *libHandle = dlopen(lib.c_str(), RTLD_NOW | RTLD_GLOBAL);

        if (libHandle == nullptr) {
          std::cerr << "Error: " << dlerror() << std::endl;
          continue;
        }*/
    }

    /*for (const auto &file : filescmds) {
        std::cout << "File: " << file.first << std::endl;
        std::cout << "Command: " << file.second << std::endl;
    }

    for (const auto &object : objects) {
        std::cout << "Object: " << object << std::endl;
    }*/

    auto compilRes = compileAndRunCodeCustom(filescmds, objects);

    if (!compilRes) {
        NotifyNotification *notification = notify_notification_new(
            "Compilation failed", // Title of the notification
            "Ninja build failed", // Body of the notification
            "/home/fabio/Downloads/icons8-fogo-96.png" // Icon path (NULL for
                                                       // default icon)
        );

        // Set the urgency level of the notification
        notify_notification_set_urgency(notification, NOTIFY_URGENCY_CRITICAL);

        // Show the notification
        notify_notification_show(notification, NULL);
    } else {
        NotifyNotification *notification = notify_notification_new(
            "Compilation success", // Title of the notification
            "Ninja build success", // Body of the notification
            "/home/fabio/Downloads/icons8-fogo-96.png" // Icon path (NULL for
                                                       // default icon)
        );

        // Set the urgency level of the notification
        notify_notification_set_urgency(notification, NOTIFY_URGENCY_NORMAL);

        // Show the notification
        notify_notification_show(notification, NULL);
    }

    commands.clear();

    dlclose(ninja);
}

static constexpr size_t INOTIFY_EVENT_SIZE = (sizeof(struct inotify_event));
static constexpr size_t MAX_BUF_LEN = (1024 * (INOTIFY_EVENT_SIZE + 16));

static std::string_view trimStrview(std::string_view str) {
    str = str.substr(str.find_first_not_of(" \t\n\v\f\r\0"));
    str = str.substr(0, str.find_last_not_of(" \t\n\v\f\r\0") + 1);

    return str;
}

bool rebuildUpdatedFile(std::chrono::steady_clock::time_point last_time,
                        const std::chrono::steady_clock::time_point now,
                        const struct inotify_event *event,
                        const std::filesystem::path &filename) {
    if ((event->mask & IN_MODIFY) == 0) {
        return false;
    }

    if (std::filesystem::file_size(filename) == 0) {
        return false;
    }

    if (filename.parent_path().string().find("build") != std::string::npos) {
        return false;
    }

    last_time = now;

    std::cout << "File modified: " << filename << " rebuilt ver" << std::endl;

    return true;
}

void loopBytesInotifyRebuildFiles(
    std::string_view file_to_watch, const char *buffer,
    std::chrono::steady_clock::time_point &last_time, int num_bytes) {
    auto now = std::chrono::steady_clock::now();
    std::filesystem::path path(file_to_watch);

    bool shouldRebuild = false;

    for (int i = 0; i < num_bytes; /**/) {
        const auto *event =
            reinterpret_cast<const struct inotify_event *>(&buffer[i]);

        std::string_view filename(event->name,
                                  strnlen(event->name, event->len));

        filename = trimStrview(filename);

        std::filesystem::path filename_path;

        if (filename.empty()) {
            filename = file_to_watch;
        } else {
            filename_path = path / filename;
        }

        std::cout << "Event: " << event->mask << "  " << filename << std::endl;

        try {
            shouldRebuild =
                shouldRebuild ||
                rebuildUpdatedFile(last_time, now, event, filename_path);
        } catch (const std::exception &e) {
            std::cerr << e.what() << std::endl;
        }

        i += INOTIFY_EVENT_SIZE + event->len;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    if (shouldRebuild) {
        ninjarebuild(false);
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

void monitorProjDir() {
    auto projdir = std::filesystem::current_path().parent_path().string();
    monitorAndRebuildFileOrDirectory(projdir);
}

void exec() {
    notify_init("Notification Example");
    ninjarebuild();
}
