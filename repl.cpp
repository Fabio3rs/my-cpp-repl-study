#include "stdafx.hpp"

#include "repl.hpp"
#include "simdjson.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <dlfcn.h>
#include <execution>
#include <filesystem>
#include <functional>
#include <mutex>
#include <numeric>
#include <string>
#include <system_error>
#include <thread>
#include <tuple>
#include <unistd.h>
#include <unordered_set>
#include <utility>

std::unordered_set<std::string> linkLibraries;
std::unordered_set<std::string> includeDirectories;
std::unordered_set<std::string> preprocessorDefinitions;

auto getLinkLibraries() -> std::unordered_set<std::string> {
    std::unordered_set<std::string> linkLibraries;

    std::fstream file("linkLibraries.txt", std::ios::in);

    if (file.is_open()) {
        std::string line;
        while (std::getline(file, line)) {
            linkLibraries.insert(line);
        }
    }

    return linkLibraries;
}

auto getLinkLibrariesStr() -> std::string {
    std::string linkLibrariesStr;

    linkLibrariesStr += " -L./ ";

    for (const auto &lib : linkLibraries) {
        linkLibrariesStr += " -l" + lib;
    }

    return linkLibrariesStr;
}

auto getIncludeDirectoriesStr() -> std::string {
    std::string includeDirectoriesStr;

    for (const auto &dir : includeDirectories) {
        includeDirectoriesStr += " -I" + dir;
    }

    return includeDirectoriesStr;
}

auto getPreprocessorDefinitionsStr() -> std::string {
    std::string preprocessorDefinitionsStr;

    for (const auto &def : preprocessorDefinitions) {
        preprocessorDefinitionsStr += " -D" + def;
    }

    return preprocessorDefinitionsStr;
}

int onlyBuildLib(std::string compiler, const std::string &name,
                 std::string ext = ".cpp", std::string std = "c++20") {
    std::string includePrecompiledHeader = " -include precompiledheader.hpp";

    if (ext == ".c") {
        includePrecompiledHeader = "";
    }

    auto cmd = compiler + " -std=" + std + " -shared " +
               includePrecompiledHeader + getIncludeDirectoriesStr() + " " +
               getPreprocessorDefinitionsStr() +
               " -g "
               "-Wl,--export-dynamic -fPIC " +
               name + ext + " " + getLinkLibrariesStr() +
               " -o "
               "lib" +
               name + ".so";

    return system(cmd.c_str());
}

int buildLibAndDumpAST(std::string compiler, const std::string &name,
                       std::string ext = ".cpp", std::string std = "c++20") {
    std::string includePrecompiledHeader = " -include precompiledheader.hpp";

    if (ext == ".c") {
        includePrecompiledHeader = "";
    }

    auto cmd = compiler + " -std=" + std + " -shared " +
               includePrecompiledHeader + getIncludeDirectoriesStr() + " " +
               getPreprocessorDefinitionsStr() +
               " -g "
               "-Wl,--export-dynamic -fPIC " +
               name + ext + " " + getLinkLibrariesStr() +
               " -o "
               "lib" +
               name + ".so";
    int buildres = system(cmd.c_str());

    if (buildres != 0) {
        return buildres;
    }

    cmd = compiler + " -std=" + std + " -fPIC -Xclang -ast-dump=json " +
          includePrecompiledHeader + getIncludeDirectoriesStr() + " " +
          getPreprocessorDefinitionsStr() + " -fsyntax-only " + name + ext +
          " -o "
          "lib" +
          name + ".so > " + name + ".json";
    return system(cmd.c_str());
}

std::string outputHeader;
std::unordered_set<std::string> includedFiles;

struct VarDecl {
    std::string name;
    std::string mangledName;
    std::string type;
    std::string qualType;
    std::string kind;
    std::string file;
    int line;
};

static std::mutex varsWriteMutex;

void analyzeInnerAST(
    std::filesystem::path source, std::vector<VarDecl> &vars,
    simdjson::simdjson_result<simdjson::ondemand::value> inner) {
    std::filesystem::path lastfile;

    if (inner.error() != simdjson::SUCCESS) {
        std::cout << "inner is not an object" << std::endl;
        return;
    }

    auto inner_type = inner.type();
    if (inner_type.error() != simdjson::SUCCESS ||
        inner_type.value() != simdjson::ondemand::json_type::array) {
        std::cout << "inner is not an array" << std::endl;
    }

    auto inner_array = inner.get_array();

    for (auto element : inner_array) {
        auto loc = element["loc"];

        if (loc.error()) {
            continue;
        }

        auto lfile = loc["file"];

        if (!lfile.error()) {
            simdjson::simdjson_result<std::string_view> lfile_string =
                lfile.get_string();

            if (!lfile_string.error()) {
                lastfile = lfile_string.value();
            }
        }

        auto includedFrom = loc["includedFrom"];

        if (!includedFrom.error()) {
            auto includedFrom_string = includedFrom["file"].get_string();

            if (!includedFrom_string.error()) {
                std::filesystem::path p(lastfile);

                try {
                    p = std::filesystem::absolute(
                        std::filesystem::canonical(p));
                } catch (...) {
                }

                std::string path = p.string();

                if (!path.empty() && p.filename() != "decl_amalgama.hpp" &&
                    p.filename() != "printerOutput.hpp" &&
                    includedFrom_string.value() == source &&
                    includedFiles.find(path) == includedFiles.end()) {
                    includedFiles.insert(path);
                    outputHeader += "#include \"" + path + "\"\n";
                }
            }
        }

        std::error_code ec;
        if (!source.empty() && !lastfile.empty() &&
            !std::filesystem::equivalent(lastfile, source, ec) && !ec) {
            try {
                auto canonical = std::filesystem::canonical(lastfile);

                auto current =
                    std::filesystem::canonical(std::filesystem::current_path());

                if (!canonical.string().starts_with(current.string())) {
                    continue;
                }
            } catch (const std::exception &e) {
                std::cerr << e.what() << std::endl;
                continue;
            } catch (...) {
                continue;
            }
        }

        auto lline = loc["line"];

        if (lline.error()) {
            continue;
        }

        auto lline_int = lline.get_int64();

        if (lline_int.error()) {
            continue;
        }

        auto kind = element["kind"];

        if (kind.error()) {
            continue;
        }

        auto kind_string = kind.get_string();

        if (kind_string.error()) {
            continue;
        }

        auto name = element["name"];

        if (name.error()) {
            continue;
        }

        auto name_string = name.get_string();

        if (name_string.error()) {
            continue;
        }

        if (kind_string.error() == simdjson::SUCCESS &&
            kind_string.value() == "CXXRecordDecl" &&
            element["inner"].error() == simdjson::SUCCESS) {
            assert(element["inner"].type() ==
                   simdjson::ondemand::json_type::array);
            analyzeInnerAST(source, vars, element["inner"]);
            continue;
        }

        auto type = element["type"];

        if (type.error()) {
            continue;
        }

        auto qualType = type["qualType"];

        if (qualType.error()) {
            continue;
        }

        auto qualType_string = qualType.get_string();

        if (qualType_string.error()) {
            continue;
        }

        auto storageClassJs = element["storageClass"];

        std::string storageClass(
            storageClassJs.error() ? "" : storageClassJs.get_string().value());

        if (storageClass == "extern" || storageClass == "static") {
            continue;
        }

        if (kind_string.value() == "FunctionDecl" ||
            kind_string.value() == "CXXMethodDecl") {
            std::scoped_lock<std::mutex> lck(varsWriteMutex);

            if (kind_string.value() != "CXXMethodDecl") {
                auto qualTypestr = std::string(qualType_string.value());

                auto parem = qualTypestr.find_first_of('(');

                if (parem == std::string::npos) {
                    continue;
                }

                qualTypestr.insert(parem, std::string(name_string.value()));

                std::cout << "extern " << qualTypestr << ";" << std::endl;

                outputHeader += "extern " + qualTypestr + ";\n";
            }

            auto mangledName = element["mangledName"];

            if (mangledName.error()) {
                continue;
            }

            VarDecl var;

            var.name = name_string.value();
            var.type = "";
            var.qualType = qualType_string.value();
            var.kind = kind_string.value();
            var.file = lastfile;
            var.line = lline_int.value();
            var.mangledName = mangledName.get_string().value();

            vars.push_back(std::move(var));
        } else if (kind_string.value() == "VarDecl") {
            std::scoped_lock<std::mutex> lck(varsWriteMutex);
            outputHeader += "#line " + std::to_string(lline_int.value()) +
                            " \"" + lastfile.string() + "\"\n";

            std::string typenamestr = std::string(qualType_string.value());

            if (auto bracket = typenamestr.find_first_of('[');
                bracket != std::string::npos) {
                typenamestr.insert(bracket,
                                   " " + std::string(name_string.value()));
            } else {
                typenamestr += " " + std::string(name_string.value());
            }

            outputHeader += "extern " + typenamestr + ";\n";

            VarDecl var;

            auto type_var = type["desugaredQualType"];

            var.name = name_string.value();
            var.type = type_var.error() ? "" : type_var.get_string().value();
            var.qualType = qualType_string.value();
            var.kind = kind_string.value();
            var.file = lastfile;
            var.line = lline_int.value();

            vars.push_back(std::move(var));
        }
    }
}

int analyzeASTFromJsonString(simdjson::padded_string_view json,
                             const std::string &source,
                             std::vector<VarDecl> &vars) {
    simdjson::ondemand::parser parser;
    simdjson::ondemand::document doc;
    auto error = parser.iterate(json).get(doc);
    if (error) {
        std::cout << error << std::endl;
        return EXIT_FAILURE;
    }
    simdjson::ondemand::json_type type;
    error = doc.type().get(type);
    if (error) {
        std::cout << error << std::endl;
        return EXIT_FAILURE;
    }

    auto outputHeaderSize = outputHeader.size();

    auto inner = doc["inner"];

    analyzeInnerAST(source, vars, inner);

    if (outputHeaderSize != outputHeader.size()) {
        std::fstream headerOutput("decl_amalgama.hpp", std::ios::out);
        headerOutput << outputHeader << std::endl;
        headerOutput.close();
    }

    return EXIT_SUCCESS;
}

int analyzeASTFile(const std::string &filename, const std::string &source,
                   std::vector<VarDecl> &vars) {
    simdjson::padded_string json;
    std::cout << "loading: " << filename << std::endl;
    auto error = simdjson::padded_string::load(filename).get(json);
    if (error) {
        std::cout << "could not load the file " << filename << std::endl;
        std::cout << "error code: " << error << std::endl;
        return EXIT_FAILURE;
    } else {
        std::cout << "loaded: " << json.size() << " bytes." << std::endl;
    }

    return analyzeASTFromJsonString(json, source, vars);
}

auto runProgramGetOutput(std::string_view cmd) {
    std::pair<std::string, int> result{{}, -1};

    FILE *pipe = popen(cmd.data(), "r");

    if (!pipe) {
        std::cerr << "popen() failed!" << std::endl;
        return result;
    }

    constexpr size_t MBPAGE2 = 1024 * 1024 * 2;

    auto &buffer = result.first;
    try {
        buffer.resize(MBPAGE2);
    } catch (const std::bad_alloc &e) {
        pclose(pipe);
        std::cerr << "bad_alloc caught: " << e.what() << std::endl;
        return result;
    }

    size_t finalSize = 0;

    while (!feof(pipe)) {
        size_t read = fread(buffer.data() + finalSize, 1,
                            buffer.size() - finalSize, pipe);

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

    result.second = pclose(pipe);

    if (result.second != 0) {
        std::cerr << "program failed! " << result.second << std::endl;
    }

    buffer.resize(finalSize);

    return result;
}

void writeHeaderPrintOverloads() {
    auto printer = R"cpp(
#pragma once
#include <iostream>
#include <vector>
#include <deque>
#include <mutex>
#include <string_view>

template <class T>
inline void printdata(const std::vector<T> &vect, std::string_view name, std::string_view type) {
    std::cout << " >> " << type << (name.empty()? "" : " ") << (name.empty()? "" : name) << ": ";
    for (const auto &v : vect) {
        std::cout << v << ' ';
    }

    std::cout << std::endl;
}

template <class T>
inline void printdata(const std::deque<T> &vect, std::string_view name, std::string_view type) {
    std::cout << " >> " << type << (name.empty()? "" : " ") << (name.empty()? "" : name) << ": ";
    for (const auto &v : vect) {
        std::cout << v << ' ';
    }

    std::cout << std::endl;
}

inline void printdata(std::string_view str, std::string_view name, std::string_view type) {
    std::cout << " >> " << type << (name.empty()? "" : " ") << (name.empty()? "" : name) << ": " << str << std::endl;
}

inline void printdata(const std::mutex &mtx, std::string_view name, std::string_view type) {
    std::cout << " >> " << (name.empty()? "" : " ") << (name.empty()? "" : name) << "Mutex" << std::endl;
}

template <class T>
inline void printdata(const T &val, std::string_view name, std::string_view type) {
    std::cout << " >> " << type << (name.empty()? "" : " ") << (name.empty()? "" : name) << ": " << val << std::endl;
}

    )cpp";

    std::fstream printerOutput("printerOutput.hpp",
                               std::ios::out | std::ios::trunc);

    printerOutput << printer << std::endl;

    printerOutput.close();
}

int build_precompiledheader(std::string compiler = "clang++") {
    std::fstream precompHeader("precompiledheader.hpp",
                               std::ios::out | std::ios::trunc);

    precompHeader << "#pragma once\n\n" << std::endl;
    precompHeader << "#include <any>\n" << std::endl;
    precompHeader << "extern int (*bootstrapProgram)(int argc, char "
                     "**argv);\n";
    precompHeader << "extern std::any lastReplResult;\n";
    precompHeader << "#include \"printerOutput.hpp\"\n\n" << std::endl;

    for (const auto &include : includedFiles) {
        if (std::filesystem::exists(include)) {
            precompHeader << "#include \"" << include << "\"\n";
        } else {
            precompHeader << "#include <" << include << ">\n";
        }
    }

    precompHeader.flush();
    precompHeader.close();

    std::string cmd = compiler + getPreprocessorDefinitionsStr() + " " +
                      getIncludeDirectoriesStr() +
                      " -fPIC -x c++-header -std=c++20 -o "
                      "precompiledheader.hpp.pch precompiledheader.hpp";
    return system(cmd.c_str());
}

std::unordered_map<std::string, void (*)()> varPrinterAddresses;

void printPrepareAllSave(const std::vector<VarDecl> &vars) {
    if (vars.empty()) {
        return;
    }

    static int i = 0;
    std::string name = "printerOutput" + std::to_string(i++);

    std::fstream printerOutput(name + ".cpp", std::ios::out | std::ios::trunc);

    printerOutput << "#include \"printerOutput.hpp\"\n\n" << std::endl;
    printerOutput << "#include \"decl_amalgama.hpp\"\n\n" << std::endl;

    for (const auto &var : vars) {
        // std::cout << __LINE__ << var.kind << std::endl;
        /*std::cout << var.qualType << "   " << var.type << "   " << var.name
                  << std::endl;*/
        if (var.kind == "VarDecl") {
            printerOutput << "extern \"C\" void printvar_" << var.name
                          << "() {\n";
            printerOutput << "  printdata(" << var.name << ", \"" << var.name
                          << "\", \"" + var.qualType + "\");\n";
            printerOutput << "}\n";
        }
    }

    printerOutput << "void printall() {\n";

    for (const auto &var : vars) {
        // std::cout << __LINE__ << var.kind << std::endl;
        if (var.kind == "VarDecl") {
            printerOutput << "printdata(" << var.name << ", \"" << var.name
                          << "\", \"" + var.qualType + "\");\n";
        }
    }

    printerOutput << "}\n";

    printerOutput.close();

    int buildLibRes = onlyBuildLib("clang++", name);

    if (buildLibRes != 0) {
        std::cerr << "buildLibRes != 0: " << name << std::endl;
        return;
    }

    void *handlep =
        dlopen(("./lib" + name + ".so").c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!handlep) {
        std::cerr << "Cannot open library: " << dlerror() << '\n';
        return;
    }

    for (const auto &var : vars) {
        // std::cout << __LINE__ << var.kind << std::endl;
        if (var.kind == "VarDecl") {
            void (*printvar)() =
                (void (*)())dlsym(handlep, ("printvar_" + var.name).c_str());
            if (!printvar) {
                std::cerr << "Cannot load symbol 'printvar_" << var.name
                          << "': " << dlerror() << '\n';
                dlclose(handlep);
                return;
            }

            varPrinterAddresses[var.name] = printvar;
        }
    }
}

void savePrintAllVarsLibrary(const std::vector<VarDecl> &vars) {
    if (vars.empty()) {
        return;
    }

    std::fstream printerOutput("printerOutput.cpp",
                               std::ios::out | std::ios::trunc);

    printerOutput << "#include \"printerOutput.hpp\"\n\n" << std::endl;
    printerOutput << "#include \"decl_amalgama.hpp\"\n\n" << std::endl;

    printerOutput << "void printall() {\n";

    for (const auto &var : vars) {
        // std::cout << __LINE__ << var.kind << std::endl;
        if (var.kind == "VarDecl") {
            printerOutput << "printdata(" << var.name << ", \"" << var.name
                          << "\", \"" + var.qualType + "\");\n";
        }
    }

    printerOutput << "}\n";

    printerOutput.close();

    onlyBuildLib("clang++", "printerOutput");
}

struct wrapperFn {
    void *fnptr;
    void **wrap_ptrfn;
};

std::unordered_set<std::string> varsNames;
std::unordered_map<std::string, wrapperFn> fnNames;
std::vector<VarDecl> allTheVariables;

auto buildASTWithPrintVarsFn(std::string compiler, const std::string &name)
    -> std::vector<VarDecl> {
    auto cmd = compiler + getPreprocessorDefinitionsStr() + " " +
               getIncludeDirectoriesStr() +
               " -std=c++20 -fPIC -Xclang -ast-dump=json -include "
               "precompiledheader.hpp -fsyntax-only " +
               name + ".cpp -o lib" + name + ".so > " + name + ".json";

    std::cout << cmd << std::endl;
    int analyzeres = system(cmd.c_str());

    if (analyzeres != 0) {
        std::cerr << "analyzeres != 0: " << cmd << std::endl;
        return {};
    }

    std::vector<VarDecl> vars;

    analyzeASTFile(name + ".json", name + ".cpp", vars);

    cmd = compiler + getPreprocessorDefinitionsStr() + " " +
          getIncludeDirectoriesStr() +
          " -std=c++20 -shared -include precompiledheader.hpp -g "
          "-Wl,--export-dynamic -fPIC " +
          name + ".cpp " + getLinkLibrariesStr() +
          " -o "
          "lib" +
          name + ".so";
    std::cout << cmd << std::endl;
    int buildres = system(cmd.c_str());

    if (buildres != 0) {
        std::cerr << "buildres != 0: " << cmd << std::endl;
        return {};
    }

    savePrintAllVarsLibrary(vars);

    // merge todasVars with vars
    for (const auto &var : vars) {
        if (varsNames.find(var.name) == varsNames.end()) {
            varsNames.insert(var.name);
            allTheVariables.push_back(var);
        }
    }

    return vars;
}

auto concatNames(const std::vector<std::string> &names) -> std::string {
    std::string concat;

    size_t size = 0;

    for (const auto &name : names) {
        size += name.size() + 1;
    }

    concat.reserve(size);

    for (const auto &name : names) {
        concat += name;
        concat += " ";
    }

    return concat;
}

void dumpCppIncludeTargetWrapper(const std::basic_string<char> &name,
                                 const std::string &wrappedname) {
    std::fstream replOutput(wrappedname, std::ios::out | std::ios::trunc);

    replOutput << "#include \"precompiledheader.hpp\"\n\n";
    replOutput << "#include \"decl_amalgama.hpp\"\n\n";

    replOutput << "#include \"" << name << "\"" << std::endl;

    replOutput.close();
}

void mergeVars(const std::vector<VarDecl> &vars) {
    for (const auto &var : vars) {
        if (varsNames.find(var.name) == varsNames.end()) {
            varsNames.insert(var.name);
            allTheVariables.push_back(var);
        }
    }
}

auto buildLibAndDumpASTWithoutPrint(std::string compiler,
                                    const std::string &libname,
                                    const std::vector<std::string> &names,
                                    const std::string &std)
    -> std::pair<std::vector<VarDecl>, int> {
    std::pair<std::vector<VarDecl>, int> resultc;

    std::string includes = getIncludeDirectoriesStr();
    std::string preprocessorDefinitions = getPreprocessorDefinitionsStr();

    auto now = std::chrono::system_clock::now();

    std::string namesConcated;

    namesConcated.reserve(std::accumulate(
        names.begin(), names.end(), 0,
        [](size_t acc, const auto &name) { return acc + name.size() + 1; }));

    std::mutex namesConcatedMutex;

    std::for_each(
        std::execution::par, names.begin(), names.end(), [&](const auto &name) {
            if (name.empty()) {
                return;
            }

            const auto path = std::filesystem::path(name);
            std::string purefilename = path.filename().string();

            purefilename =
                purefilename.substr(0, purefilename.find_last_of('.'));

            std::thread analyzeThr([&]() {
                std::string logname = purefilename + ".log";
                std::string cmd =
                    compiler + preprocessorDefinitions + " " + includes +
                    " -std=" + std +
                    " -fPIC -Xclang -ast-dump=json "
                    " -Xclang -include-pch -Xclang precompiledheader.hpp.pch "
                    "-include precompiledheader.hpp -fsyntax-only " +
                    name + " -o lib" + purefilename + "_blank.so 2>" + logname;

                auto out = runProgramGetOutput(cmd);

                if (out.second != 0) {
                    std::cerr << "runProgramGetOutput(cmd) != 0: " << out.second
                              << " " << cmd << std::endl;
                    std::fstream file(logname, std::ios::in);

                    std::copy(std::istreambuf_iterator<char>(file),
                              std::istreambuf_iterator<char>(),
                              std::ostreambuf_iterator<char>(std::cerr));
                    resultc.second = out.second;
                    return;
                }

                simdjson::padded_string_view json(out.first);
                analyzeASTFromJsonString(json, name, resultc.first);
            });

            try {
                std::string object = purefilename + ".o";

                {
                    std::scoped_lock<std::mutex> lck(namesConcatedMutex);
                    namesConcated += object + " ";
                }

                std::string cmd =
                    compiler + preprocessorDefinitions + " " + includes +
                    " -std=c++20 -fPIC  -c -Xclang "
                    "-include-pch -Xclang precompiledheader.hpp.pch "
                    "-include precompiledheader.hpp "
                    "-g -fPIC " +
                    name + " -o " + object;

                int buildres = system(cmd.c_str());

                if (buildres != 0) {
                    resultc.second = buildres;
                    std::cerr << "buildres != 0: " << cmd << std::endl;
                    std::cout << name << std::endl;
                }
            } catch (const std::exception &e) {
                std::cerr << e.what() << std::endl;
                resultc.second = -1;
            }

            if (analyzeThr.joinable()) {
                analyzeThr.join();
            }
        });

    if (resultc.second != 0) {
        return resultc;
    }

    auto end = std::chrono::system_clock::now();

    std::cout << "Analyze and compile time: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(end -
                                                                       now)
                     .count()
              << "ms\n";

    std::string linkLibraries = getLinkLibrariesStr();

    std::string cmd;

    cmd = compiler +
          " -shared -g "
          "-WL,--export-dynamic " +
          namesConcated + " " + getLinkLibrariesStr() +
          " -o "
          "lib" +
          libname + ".so";
    std::cout << cmd << std::endl;
    int linkRes = system(cmd.c_str());

    if (linkRes != 0) {
        std::cerr << "linkRes != 0: " << cmd << std::endl;
        return resultc;
    }

    mergeVars(resultc.first);

    return resultc;
}

int runPrintAll() {
    void *handlep = dlopen("./libprinterOutput.so", RTLD_NOW | RTLD_GLOBAL);
    if (!handlep) {
        std::cerr << "Cannot open library: " << dlerror() << '\n';
        return EXIT_FAILURE;
    }

    void (*printall)() = (void (*)())dlsym(handlep, "_Z8printallv");
    if (!printall) {
        std::cerr << "Cannot load symbol 'printall': " << dlerror() << '\n';
        dlclose(handlep);
        return EXIT_FAILURE;
    }

    printall();

    dlclose(handlep);

    return EXIT_SUCCESS;
}

void prepareFunctionWrapper(
    const std::string &name, const std::vector<VarDecl> &vars,
    std::unordered_map<std::string, std::string> &functions) {
    std::string wrapper;

    wrapper += "#include \"decl_amalgama.hpp\"\n\n";

    std::unordered_set<std::string> addedFns;

    for (const auto &fnvars : vars) {
        std::cout << fnvars.name << std::endl;
        if (fnvars.kind != "FunctionDecl" && fnvars.kind != "CXXMethodDecl") {
            continue;
        }

        if (fnvars.mangledName == "main") {
            continue;
        }

        if (addedFns.contains(fnvars.mangledName)) {
            continue;
        }

        if (!fnNames.contains(fnvars.mangledName)) {
            addedFns.insert(fnvars.mangledName);
            auto qualTypestr = std::string(fnvars.qualType);
            auto parem = qualTypestr.find_first_of('(');

            if (parem == std::string::npos || fnvars.kind != "FunctionDecl") {
                qualTypestr = "extern \"C\" void __attribute__ ((naked)) " +
                              fnvars.mangledName + "()";
            } else {
                qualTypestr.insert(parem,
                                   std::string(" __attribute__ ((naked)) " +
                                               fnvars.mangledName));
                qualTypestr.insert(0, "extern \"C\" ");
            }

            wrapper += "extern \"C\" void *" + fnvars.mangledName +
                       "_ptr = nullptr;\n\n";
            wrapper += qualTypestr + " {\n";
            wrapper += R"(    __asm__ __volatile__ (
        "jmp *%0\n"
        :
        : "r" ()" + fnvars.mangledName +
                       R"(_ptr)
    );
}
)";
        }

        functions[fnvars.mangledName] = fnvars.name;
    }

    if (!functions.empty()) {
        auto wrappername = "wrapper_" + name;
        std::fstream wrapperOutput(wrappername + ".cpp",
                                   std::ios::out | std::ios::trunc);
        wrapperOutput << wrapper << std::endl;
        wrapperOutput.close();

        onlyBuildLib("clang++", wrappername);
    }
}

void fillWrapperPtrs(std::unordered_map<std::string, std::string> &functions,
                     void *handlewp, void *handle) {
    for (const auto &[mangledName, fns] : functions) {
        void *fnptr = dlsym(handle, mangledName.c_str());
        if (!fnptr) {
            void **wrap_ptrfn =
                (void **)dlsym(handlewp, (fns + "_ptr").c_str());

            if (!wrap_ptrfn) {
                continue;
            }

            if (!fnNames.contains(mangledName)) {
                continue;
            }

            auto &fn = fnNames[mangledName];
            fn.fnptr = nullptr;
            fn.wrap_ptrfn = wrap_ptrfn;
            continue;
        }

        if (fnNames.contains(mangledName)) {
            auto it = fnNames.find(mangledName);
            it->second.fnptr = fnptr;

            void **wrap_ptrfn = it->second.wrap_ptrfn;

            if (wrap_ptrfn) {
                *wrap_ptrfn = fnptr;
            }
            continue;
        }

        auto &fn = fnNames[mangledName];

        fn.fnptr = fnptr;

        void **wrap_ptrfn =
            (void **)dlsym(handlewp, (mangledName + "_ptr").c_str());

        if (!wrap_ptrfn) {
            std::cerr << "Cannot load symbol '" << mangledName
                      << "_ptr': " << dlerror() << '\n';
            continue;
        }

        *wrap_ptrfn = fnptr;
        fn.wrap_ptrfn = wrap_ptrfn;
    }
}

std::any lastReplResult;
std::vector<std::function<bool()>> lazyEvalFns;
bool shouldRecompilePrecompiledHeader = false;

struct CompilerCodeCfg {
    std::string compiler = "clang++";
    std::string std = "c++20";
    std::string extension = "cpp";
    std::string repl_name;
    std::string libraryName;
    std::string wrapperName;
    std::vector<std::string> sourcesList;
    bool analyze = true;
    bool addIncludes = true;
    bool fileWrap = true;
    bool lazyEval = false;
};

void evalEverything() {
    for (const auto &fn : lazyEvalFns) {
        fn();
    }

    lazyEvalFns.clear();
}

auto prepareWraperAndLoadCodeLib(const CompilerCodeCfg &cfg,
                                 std::vector<VarDecl> &&vars) {
    std::unordered_map<std::string, std::string> functions;

    prepareFunctionWrapper(cfg.repl_name, vars, functions);

    void *handlewp = nullptr;

    if (!functions.empty()) {
        handlewp = dlopen(("./libwrapper_" + cfg.repl_name + ".so").c_str(),
                          RTLD_NOW | RTLD_GLOBAL);
    }

    int dlOpenFlags = RTLD_NOW | RTLD_GLOBAL;

    if (cfg.lazyEval) {
        std::cout << "lazyEval:  " << cfg.repl_name << std::endl;
        dlOpenFlags = RTLD_LAZY | RTLD_GLOBAL;
    }

    auto load_start = std::chrono::steady_clock::now();

    void *handle =
        dlopen(("./lib" + cfg.repl_name + ".so").c_str(), dlOpenFlags);
    if (!handle) {
        std::cerr << __FILE__ << ":" << __LINE__
                  << " Cannot open library: " << dlerror() << '\n';
        return false;
    }

    auto load_end = std::chrono::steady_clock::now();

    std::cout << "load time: "
              << std::chrono::duration_cast<std::chrono::microseconds>(
                     load_end - load_start)
                     .count()
              << "us" << std::endl;

    auto eval = [functions = std::move(functions), handlewp = handlewp,
                 handle = handle, vars = std::move(vars)]() mutable {
        fillWrapperPtrs(functions, handlewp, handle);

        printPrepareAllSave(vars);

        void (*execv)() = (void (*)())dlsym(handle, "_Z4execv");
        if (execv || (execv = (void (*)())dlsym(handle, "exec"))) {
            auto exec_start = std::chrono::steady_clock::now();
            try {
                execv();
            } catch (const std::exception &e) {
                std::cerr << "C++ exception on exec/eval: " << e.what()
                          << std::endl;
            } catch (...) {
                std::cerr << "Unknown C++ exception on exec/eval" << std::endl;
            }

            auto exec_end = std::chrono::steady_clock::now();

            std::cout << "exec time: "
                      << std::chrono::duration_cast<std::chrono::microseconds>(
                             exec_end - exec_start)
                             .count()
                      << "us" << std::endl;
        }

        for (const auto &var : vars) {
            if (var.kind != "VarDecl") {
                continue;
            }

            auto it = varPrinterAddresses.find(var.name);

            if (it != varPrinterAddresses.end()) {
                it->second();
            } else {
                std::cout << "not found: " << var.name << std::endl;
            }
        }

        std::cout << std::endl;

        return true;
    };

    if (cfg.lazyEval) {
        lazyEvalFns.push_back(eval);
    } else {
        eval();
    }

    return true;
}

auto compileAndRunCode(CompilerCodeCfg &&cfg) {
    std::vector<VarDecl> vars;
    int returnCode = 0;

    auto now = std::chrono::steady_clock::now();

    if (cfg.sourcesList.empty()) {
        if (cfg.analyze) {
            std::tie(vars, returnCode) = buildLibAndDumpASTWithoutPrint(
                cfg.compiler, cfg.repl_name, {cfg.repl_name + ".cpp"}, cfg.std);
        } else {
            onlyBuildLib(cfg.compiler, cfg.repl_name, "." + cfg.extension,
                         cfg.std);
        }
    } else {
        std::tie(vars, returnCode) = buildLibAndDumpASTWithoutPrint(
            cfg.compiler, cfg.repl_name, cfg.sourcesList, cfg.std);
    }

    if (returnCode != 0) {
        return false;
    }

    auto end = std::chrono::steady_clock::now();

    std::cout << "build time: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(end -
                                                                       now)
                     .count()
              << "ms" << std::endl;

    return prepareWraperAndLoadCodeLib(cfg, std::move(vars));
}

auto execRepl(std::string_view lineview, int64_t &i) -> bool {
    std::string line(lineview);
    if (line == "exit") {
        return false;
    }

    if (line.starts_with("#includedir ")) {
        line = line.substr(12);

        includeDirectories.insert(line);
        return true;
    }

    if (line.starts_with("#compilerdefine ")) {
        line = line.substr(16);

        preprocessorDefinitions.insert(line);
        return true;
    }

    if (line.starts_with("#include")) {
        std::regex includePattern(R"(#include\s*["<]([^">]+)[">])");
        std::smatch match;

        if (std::regex_search(line, match, includePattern)) {
            if (match.size() > 1) {
                std::cout << "File name: " << match[1] << std::endl;

                std::filesystem::path p(match[1]);

                try {
                    p = std::filesystem::absolute(
                        std::filesystem::canonical(p));
                } catch (...) {
                }

                std::string path = p.string();

                if (p.filename() != "decl_amalgama.hpp" &&
                    p.filename() != "printerOutput.hpp") {
                    shouldRecompilePrecompiledHeader =
                        includedFiles.insert(path).second;
                }
            } else {
                std::cout << "File name not found" << std::endl;
                return true;
            }
        } else {
            std::cout << "File name not found" << std::endl;
            return true;
        }
        return true;
    }

    if (shouldRecompilePrecompiledHeader) {
        build_precompiledheader();
        shouldRecompilePrecompiledHeader = false;
    }

    if (line == "printall") {
        std::cout << "printall" << std::endl;

        savePrintAllVarsLibrary(allTheVariables);
        runPrintAll();
        return true;
    }

    if (line == "evalall") {
        evalEverything();
        return true;
    }

    if (line.starts_with("#lib ")) {
        line = line.substr(5);

        linkLibraries.insert(line);
        return true;
    }

    if (varsNames.contains(line)) {
        auto it = varPrinterAddresses.find(line);

        if (it != varPrinterAddresses.end()) {
            it->second();
            return true;
        } else {
            std::cout << "not found" << std::endl;
        }

        std::cout << "printdata(" << line << ");" << std::endl;

        std::fstream printerOutput("printerOutput.cpp",
                                   std::ios::out | std::ios::trunc);

        printerOutput << "#include \"decl_amalgama.hpp\"\n\n" << std::endl;

        printerOutput << "void printall() {\n";

        printerOutput << "printdata(" << line << ");\n";

        printerOutput << "}\n";

        printerOutput.close();

        onlyBuildLib("clang++", "printerOutput");

        runPrintAll();

        return true;
    }

    CompilerCodeCfg cfg;

    cfg.lazyEval = line.starts_with("#lazyeval ");

    if (line.starts_with("#batch_eval ")) {
        line = line.substr(11);

        std::string file;
        std::istringstream iss(line);
        while (std::getline(iss, file, ' ')) {
            cfg.sourcesList.push_back(file);
        }

        cfg.addIncludes = false;
        cfg.fileWrap = false;
    }

    if (line.starts_with("#eval ") || line.starts_with("#lazyeval ")) {
        line = line.substr(line.find_first_of(' ') + 1);
        line = line.substr(0, line.find_last_not_of(" \t\n\v\f\r\0") + 1);

        if (std::filesystem::exists(line)) {
            cfg.fileWrap = false;

            cfg.sourcesList.push_back(line);
            auto textension = line.substr(line.find_last_of('.') + 1);
            /*std::fstream file(line, std::ios::in);


            line.clear();
            std::copy(std::istreambuf_iterator<char>(file),
                      std::istreambuf_iterator<char>(),
                      std::back_inserter(line));*/

            std::cout << "extension: " << textension << std::endl;

            if (textension == "h" || textension == "hpp") {
                cfg.addIncludes = false;
            }

            if (textension == "c") {
                cfg.extension = textension;
                cfg.analyze = false;
                cfg.addIncludes = false;

                cfg.std = "c17";
                cfg.compiler = "clang";
            }
        } else {
            line = "void exec() { " + line + "; }\n";
            cfg.analyze = false;
        }
    }

    if (line.starts_with("#return ")) {
        line = line.substr(8);

        line = "void exec() { printdata(((" + line +
               ")), \"custom\", typeid(decltype((" + line + "))).name()); }\n";

        cfg.analyze = false;
    }

    // std::cout << line << std::endl;

    cfg.repl_name = "repl_" + std::to_string(i++);

    if (cfg.fileWrap) {
        std::fstream replOutput(cfg.repl_name + "." + cfg.extension,
                                std::ios::out | std::ios::trunc);

        if (cfg.addIncludes) {
            replOutput << "#include \"precompiledheader.hpp\"\n\n";
            replOutput << "#include \"decl_amalgama.hpp\"\n\n";
        }

        replOutput << line << std::endl;

        replOutput.close();
    }

    compileAndRunCode(std::move(cfg));

    return true;
}

int __attribute__((visibility("default"))) (*bootstrapProgram)(
    int argc, char **argv) = nullptr;

int64_t replCounter = 0;

auto extExecRepl(std::string_view lineview) -> bool {
    return execRepl(lineview, replCounter);
}

void repl() {
    std::string line;

    while (true) {
        std::cout << ">>> ";
        if (!std::getline(std::cin, line)) {
            break;
        }

        if (!extExecRepl(line)) {
            break;
        }

        if (bootstrapProgram) {
            break;
        }
    }
}

void initRepl() {
    writeHeaderPrintOverloads();
    build_precompiledheader();

    outputHeader.reserve(1024 * 1024);
    outputHeader += "#pragma once\n";
    outputHeader += "#include \"precompiledheader.hpp\"\n";

    std::fstream printerOutput("decl_amalgama.hpp",
                               std::ios::out | std::ios::trunc);
    printerOutput << outputHeader << std::endl;
    printerOutput.close();
}

std::any getResultRepl(std::string cmd) {
    lastReplResult = std::any();

    cmd.insert(0, "#eval lastReplResult = (");
    cmd += ");";

    execRepl(cmd, replCounter);

    return lastReplResult;
}
