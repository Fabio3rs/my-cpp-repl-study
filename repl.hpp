#pragma once

#include <any>
#include <functional>
#include <future>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

extern int
    __attribute__((visibility("default"))) (*bootstrapProgram)(int argc,
                                                               char **argv);

extern int verbosityLevel;

struct EvalResult {
    std::string libpath;
    std::function<void()> exec;
    void *handle{};
    bool success{false};

    operator bool() const { return success; }
};

void initNotifications(std::string_view appName);
void notifyError(std::string_view summary, std::string_view msg);
void initRepl();
void shutdownRepl();
void repl();
bool extExecRepl(std::string_view cmd);
void installCtrlCHandler();

struct VarDecl {
    std::string name;
    std::string mangledName;
    std::string type;
    std::string qualType;
    std::string kind;
    std::string file;
    int line;
};

struct CompilerCodeCfg {
    std::string compiler = "clang++";
    std::string std = "gnu++20";
    std::string extension = "cpp";
    std::string repl_name;
    std::string libraryName;
    std::string wrapperName;
    std::vector<std::string> sourcesList;
    bool analyze = true;
    bool addIncludes = true;
    bool fileWrap = true;
    bool lazyEval = false;
    bool use_cpp2 = false;
};

struct BuildSettings {
    std::unordered_set<std::string> linkLibraries;
    std::unordered_set<std::string> includeDirectories;
    std::unordered_set<std::string> preprocessorDefinitions;
    std::unordered_set<std::string> extraLinkerFlags;

    std::string getLinkLibrariesStr() const {
        std::string linkLibrariesStr;
        linkLibrariesStr += " -L./ ";
        for (const auto &lib : linkLibraries) {
            linkLibrariesStr += " -l" + lib;
        }
        return linkLibrariesStr;
    }

    std::string getIncludeDirectoriesStr() const {
        std::string includeDirectoriesStr;
        for (const auto &dir : includeDirectories) {
            includeDirectoriesStr += " -I" + dir;
        }
        return includeDirectoriesStr;
    }

    std::string getPreprocessorDefinitionsStr() const {
        std::string preprocessorDefinitionsStr;
        for (const auto &def : preprocessorDefinitions) {
            preprocessorDefinitionsStr += " -D" + def;
        }
        return preprocessorDefinitionsStr;
    }

    std::string getExtraLinkerFlags() const {
        std::string extraLinkerFlagsStr;
        for (const auto &flag : extraLinkerFlags) {
            extraLinkerFlagsStr += " " + flag;
        }
        return extraLinkerFlagsStr;
    }
};

struct ReplState {
    bool useCpp2 = false;
    bool shouldRecompilePrecompiledHeader = false;

    std::unordered_set<std::string> varsNames;
    std::vector<VarDecl> allTheVariables;
    std::unordered_map<std::string, void (*)()> varPrinterAddresses;
    std::unordered_map<std::string, EvalResult> evalResults;
    std::vector<std::function<bool()>> lazyEvalFns;
    std::unordered_set<std::string> includedFiles;
    // Async precompiled header rebuild control
    bool asyncPrecompiledHeaderRebuild = true;
    std::future<int>
        pchRebuildFuture; // valid() == true if a rebuild is in progress
};

auto analyzeCustomCommands(
    const std::unordered_map<std::string, std::string> &commands)
    -> std::pair<std::vector<VarDecl>, int>;

auto linkAllObjects(const std::vector<std::string> &objects,
                    const std::string &libname) -> int;

auto compileAndRunCodeCustom(
    const std::unordered_map<std::string, std::string> &commands,
    const std::vector<std::string> &objects) -> EvalResult;

void addIncludeDirectory(const std::string &dir);

// Control async precompiled header rebuild behavior. When disabled, rebuilds
// happen synchronously. If disabling while a rebuild future exists, the call
// will wait for completion.
void set_async_pch_rebuild(bool enabled) noexcept;

// Wait for any in-progress async PCH rebuild to finish (no-op if none).
void wait_for_pch_rebuild_if_running() noexcept;

std::any getResultRepl(std::string cmd);

int ext_build_precompiledheader();

extern std::any lastReplResult;
