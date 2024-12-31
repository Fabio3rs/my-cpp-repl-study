#pragma once

#include <any>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

extern int
    __attribute__((visibility("default"))) (*bootstrapProgram)(int argc,
                                                               char **argv);

struct EvalResult {
    std::string libpath;
    std::function<void()> exec;
    void *handle{};
    bool success{};

    operator bool() const { return success; }
};

void initNotifications(std::string_view appName);
void notifyError(std::string_view summary, std::string_view msg);
void initRepl();
void repl();
bool extExecRepl(std::string_view cmd);

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

auto analyzeCustomCommands(
    const std::unordered_map<std::string, std::string> &commands)
    -> std::pair<std::vector<VarDecl>, int>;

auto linkAllObjects(const std::vector<std::string> &objects,
                    const std::string &libname) -> int;

auto compileAndRunCodeCustom(
    const std::unordered_map<std::string, std::string> &commands,
    const std::vector<std::string> &objects) -> EvalResult;

void addIncludeDirectory(const std::string &dir);

std::any getResultRepl(std::string cmd);

int ext_build_precompiledheader();
