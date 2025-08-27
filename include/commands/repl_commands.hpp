#pragma once

#include <iostream>
#include <string>
#include <string_view>

#include "commands/command_registry.hpp"

// Forward declaration provided by repl.cpp
bool loadPrebuilt(const std::string &path);

namespace repl_commands {

struct ReplCtxView {
    std::unordered_set<std::string> *includeDirectories;
    std::unordered_set<std::string> *preprocessorDefinitions;
    std::unordered_set<std::string> *linkLibraries;
    bool *useCpp2Ptr;
};

inline void registerReplCommands(ReplCtxView *) {
    static bool done = false;
    if (done)
        return;
    done = true;

    commands::registry().registerPrefix(
        "#includedir ", "Add include directory",
        [](std::string_view arg, commands::CommandContextBase &base) {
            auto &ctx =
                static_cast<commands::BasicContext<ReplCtxView> &>(base).data;
            ctx.includeDirectories->insert(std::string(arg));
            return true;
        });

    commands::registry().registerPrefix(
        "#compilerdefine ", "Add compiler definition",
        [](std::string_view arg, commands::CommandContextBase &base) {
            auto &ctx =
                static_cast<commands::BasicContext<ReplCtxView> &>(base).data;
            ctx.preprocessorDefinitions->insert(std::string(arg));
            return true;
        });

    commands::registry().registerPrefix(
        "#lib ", "Link library name (without lib prefix)",
        [](std::string_view arg, commands::CommandContextBase &base) {
            auto &ctx =
                static_cast<commands::BasicContext<ReplCtxView> &>(base).data;
            ctx.linkLibraries->insert(std::string(arg));
            return true;
        });

    commands::registry().registerPrefix(
        "#loadprebuilt ", "Load prebuilt library",
        [](std::string_view arg, commands::CommandContextBase &) {
            return loadPrebuilt(std::string(arg));
        });

    commands::registry().registerPrefix(
        "#cpp2", "Enable cpp2 mode",
        [](std::string_view, commands::CommandContextBase &base) {
            auto &ctx =
                static_cast<commands::BasicContext<ReplCtxView> &>(base).data;
            *(ctx.useCpp2Ptr) = true;
            return true;
        });

    commands::registry().registerPrefix(
        "#cpp1", "Disable cpp2 mode",
        [](std::string_view, commands::CommandContextBase &base) {
            auto &ctx =
                static_cast<commands::BasicContext<ReplCtxView> &>(base).data;
            *(ctx.useCpp2Ptr) = false;
            return true;
        });

    commands::registry().registerPrefix(
        "#help", "List available commands",
        [](std::string_view, commands::CommandContextBase &) {
            const auto &es = commands::registry().entries();
            for (const auto &e : es) {
                std::cout << e.prefix << " - " << e.description << std::endl;
            }
            return true;
        });
}

inline bool handleReplCommand(std::string_view line, ReplCtxView view) {
    registerReplCommands(&view);
    return commands::handleCommand(line, view);
}

} // namespace repl_commands
