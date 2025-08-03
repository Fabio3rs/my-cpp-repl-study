#include "stdafx.hpp"

#include "backtraced_exceptions.hpp"
#include "printerOverloads.hpp"

#include "repl.hpp"
#include "simdjson.h"
#include "utility/assembly_info.hpp"
#include "utility/quote.hpp"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <exceptdefs.h>
#include <execinfo.h>
#include <execution>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <mutex>
#include <numeric>
#include <readline/history.h>
#include <readline/readline.h>
#include <segvcatch.h>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>

std::unordered_set<std::string> linkLibraries;
std::unordered_set<std::string> includeDirectories;
std::unordered_set<std::string> preprocessorDefinitions;

std::unordered_map<std::string, EvalResult> evalResults;

void addIncludeDirectory(const std::string &dir) {
    includeDirectories.insert(dir);
}

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
                 std::string ext = ".cpp", std::string std = "gnu++20") {
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
                       std::string ext = ".cpp", std::string std = "gnu++20") {
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

                if (!path.empty() && !path.ends_with(".cpp") &&
                    !path.ends_with(".cc") &&
                    p.filename() != "decl_amalgama.hpp" &&
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
            auto spellingLoc = loc["spellingLoc"];

            if (spellingLoc.error()) {
                continue;
            }

            lline = spellingLoc["line"];

            if (lline.error()) {
                continue;
            }

            loc = spellingLoc;
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
    {
        std::fstream debugOutput("debug_output.json",
                                 std::ios::out | std::ios::trunc);
        debugOutput << json << std::endl;
    }
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
                      " -fPIC -x c++-header -std=gnu++20 -o "
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
               " -std=gnu++20 -fPIC -Xclang -ast-dump=json -include "
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
          " -std=gnu++20 -shared -include precompiledheader.hpp -g "
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

auto analyzeCustomCommands(
    const std::unordered_map<std::string, std::string> &commands)
    -> std::pair<std::vector<VarDecl>, int> {
    std::pair<std::vector<VarDecl>, int> resultc;

    std::for_each(
        std::execution::par_unseq, commands.begin(), commands.end(),
        [&](const auto &namecmd) {
            if (namecmd.first.empty()) {
                return;
            }
            const auto path = std::filesystem::path(namecmd.first);
            std::string purefilename = path.filename().string();

            purefilename =
                purefilename.substr(0, purefilename.find_last_of('.'));

            std::string logname = purefilename + ".log";
            std::string cmd = namecmd.second;

            if ((namecmd.second.find("-std=gnu++20") != std::string::npos ||
                 namecmd.second.find("-std=") == std::string::npos) &&
                namecmd.second.find("-fvisibility=hidden") ==
                    std::string::npos) {
                cmd += " -std=gnu++20 -include "
                       "precompiledheader.hpp";
            }

            cmd += " -Xclang -ast-dump=json  -fsyntax-only "
                   " 2>" +
                   logname;

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
            analyzeASTFromJsonString(json, namecmd.first, resultc.first);
        });

    return resultc;
}

auto linkAllObjects(const std::vector<std::string> &objects,
                    const std::string &libname) -> int {
    std::string linkLibraries = getLinkLibrariesStr();

    std::string namesConcated = concatNames(objects);

    std::string cmd;

    cmd = "clang++ -shared -g -WL,--export-dynamic " + namesConcated + " " +
          linkLibraries + " -o lib" + libname + ".so";
    std::cout << cmd << std::endl;
    return system(cmd.c_str());
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
        std::execution::par_unseq, names.begin(), names.end(),
        [&](const auto &name) {
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
                    " -std=gnu++20 -fPIC  -c -Xclang "
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

#define MAX_LINE_LENGTH 1024

auto get_library_start_address(const char *library_name) -> uintptr_t {
    std::string line(MAX_LINE_LENGTH, 0);
    std::string library_path(MAX_LINE_LENGTH, 0);
    uintptr_t start_address{}, end_address{};

    // Open /proc/self/maps
    FILE *maps_file = fopen("/proc/self/maps", "r");
    if (maps_file == NULL) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    // Search for the library in memory maps
    while (fgets(line.data(), MAX_LINE_LENGTH, maps_file) != NULL) {
        library_path.front() = 0;
        line.back() = 0;
        line[strcspn(line.data(), "\n")] = 0;
        std::cout << strlen(line.c_str()) << "   \"" << line << '"'
                  << std::endl;
        try {
            sscanf(line.c_str(), "%zx-%zx %*s %*s %*s %*s %1023s",
                   &start_address, &end_address, library_path.data());
            std::error_code ec;
            if (std::filesystem::equivalent(library_path, library_name, ec) &&
                !ec) {
                break;
            }
        } catch (...) {
        }
    }

    if (library_path.front() == 0) {
        printf("Library %s not found in memory maps.\n", library_name);
        return 0;
    }

    fclose(maps_file);

    return start_address;
}

auto get_symbol_address(const char *library_name, const char *symbol_name)
    -> uintptr_t {
    FILE *maps_file{};
    char line[MAX_LINE_LENGTH]{};
    char library_path[MAX_LINE_LENGTH]{};
    uintptr_t start_address{}, end_address{};
    char command[MAX_LINE_LENGTH]{};
    uintptr_t symbol_offset = 0;

    // Open /proc/self/maps
    maps_file = fopen("/proc/self/maps", "r");
    if (maps_file == NULL) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    // Search for the library in memory maps
    while (fgets(line, MAX_LINE_LENGTH, maps_file) != NULL) {
        *library_path = 0;
        int readed = sscanf(line, "%zx-%zx %*s %*s %*s %*s %s", &start_address,
                            &end_address, library_path);
        std::cout << readed << "   " << library_path << "   " << library_name
                  << std::endl;
        if (strnlen(library_path, std::size(library_path)) == 0) {
            continue;
        }

        if (std::filesystem::equivalent(library_path, library_name)) {
            // Get the base address of the library
            break;
        }
    }

    if (strlen(library_path) == 0) {
        printf("Library %s not found in memory maps.\n", library_name);
        return 0;
    }

    // Get the address of the symbol within the library using nm
    sprintf(command, "nm -D --defined-only %s | grep ' %s$'", library_path,
            symbol_name);
    FILE *symbol_address_command = popen(command, "r");
    if (symbol_address_command == NULL) {
        perror("popen");
        exit(EXIT_FAILURE);
    }

    // Parse the output of nm command to get the symbol address
    if (fgets(line, MAX_LINE_LENGTH, symbol_address_command) != NULL) {
        char address[17]; // Assuming address length of 16 characters
        sscanf(line, "%16s", address);
        sscanf(address, "%zx",
               &symbol_offset); // Convert hex string to unsigned long
        printf("Address of symbol %s in %s: 0x%zx\n", symbol_name, library_name,
               start_address + symbol_offset);
    } else {
        printf("Symbol %s not found in %s.\n", symbol_name, library_name);
    }

    pclose(symbol_address_command);

    fclose(maps_file);

    return start_address + symbol_offset;
}

/*
 * Added to [try to] solve the problem of the function not being loaded when the
 * library initializes
 */
std::string lastLibrary;
std::unordered_map<std::string, uintptr_t> symbolsToResolve;

extern "C" void loadfnToPtr(void **ptr, const char *name) {
    /*
     * Function related to loadFn_"funcion name", this function is called when
     * the library constructor tries to run and the function is not loaded yet.
     */
    std::cout << __LINE__ << ": Function segfaulted: " << name
              << "   library: " << lastLibrary << std::endl;

    /*
     * TODO: Test again with dlopen RTLD_NOLOAD and dlsym
     */
    auto base = get_library_start_address(lastLibrary.c_str());

    if (base == 0) {
        std::cerr << __LINE__ << ": base == 0" << lastLibrary << std::endl;

        auto handle = dlopen(lastLibrary.c_str(), RTLD_NOLOAD);

        if (handle == nullptr) {
            std::cerr << __LINE__ << ": handle == nullptr" << lastLibrary
                      << std::endl;
            return;
        }

        for (const auto &[symbol, offset] : symbolsToResolve) {
            void **wrap_ptrfn =
                (void **)dlsym(RTLD_DEFAULT, (symbol + "_ptr").c_str());

            if (wrap_ptrfn == nullptr) {
                continue;
            }

            auto tmp = dlsym(handle, symbol.c_str());

            if (tmp == nullptr) {
                std::cerr << __LINE__ << ": tmp == nullptr" << lastLibrary
                          << std::endl;
                continue;
            }

            *wrap_ptrfn = tmp;
        }

        if (*ptr == nullptr) {
            *ptr = dlsym(handle, name);
        }
        return;
    }

    for (const auto &[symbol, offset] : symbolsToResolve) {
        void **wrap_ptrfn =
            (void **)dlsym(RTLD_DEFAULT, (symbol + "_ptr").c_str());

        if (wrap_ptrfn == nullptr) {
            continue;
        }

        *wrap_ptrfn = reinterpret_cast<void *>(base + offset);
    }
}

void prepareFunctionWrapper(
    const std::string &name, const std::vector<VarDecl> &vars,
    std::unordered_map<std::string, std::string> &functions) {
    std::string wrapper;

    // wrapper += "#include \"decl_amalgama.hpp\"\n\n";

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

            // if (parem == std::string::npos || fnvars.kind != "FunctionDecl")
            // {
            qualTypestr = "extern \"C\" void __attribute__ ((naked)) " +
                          fnvars.mangledName + "()";
            /*} else {
                qualTypestr.insert(parem,
                                   std::string(" __attribute__ ((naked)) " +
                                               fnvars.mangledName));
                qualTypestr.insert(0, "extern \"C\" ");
            }*/

            /*
             * This is the function that will be called when the library
             * initializes and the functions are not ready yet, it will try to
             * load the function address and set it to the pointer
             */
            wrapper += "static void __attribute__((naked)) loadFn_" +
                       fnvars.mangledName + "();\n";

            wrapper += "extern \"C\" void *" + fnvars.mangledName +
                       "_ptr = reinterpret_cast<void*>(loadFn_" +
                       fnvars.mangledName + ");\n\n";
            wrapper += qualTypestr + " {\n";
            wrapper += R"(    __asm__ __volatile__ (
        "jmp *%0\n"
        :
        : "r" ()" + fnvars.mangledName +
                       R"(_ptr)
    );
}
)";

            wrapper += "static void __attribute__((naked)) loadFn_" +
                       fnvars.mangledName + "() {\n";

            /*
             * Observations: Keep the stack aligned in 16 bytes before calling a
             * function
             */
            wrapper += R"(
    __asm__(
        // Save all general-purpose registers
        "pushq   %rax                \n"
        "pushq   %rbx                \n"
        "pushq   %rcx                \n"
        "pushq   %rdx                \n"
        "pushq   %rsi                \n"
        "pushq   %rdi                \n"
        "pushq   %rbp                \n"
        "pushq   %r8                 \n"
        "pushq   %r9                 \n"
        "pushq   %r10                \n"
        "pushq   %r11                \n"
        "pushq   %r12                \n"
        "pushq   %r13                \n"
        "pushq   %r14                \n"
        "pushq   %r15                \n"
        "movq    %rsp, %rbp          \n" // Set Base Pointer
    );
        // Push parameters onto the stack
        //"leaq    _Z21qRegisterResourceDataiPKhS0_S0__ptr(%rip), %rax  \n" // Address of pointer
    __asm__ __volatile__ (
        "movq %0, %%rax"
        :
        : "r" (&)" + fnvars.mangledName +
                       R"(_ptr)
    );

    __asm__(
        // Push parameters onto the stack
        //"leaq    )" + fnvars.mangledName +
                       R"(_ptr(%rip), %rax  \n" // Address
                                                                          // of
                                                                          // pointer
        "movq    %rax, %rdi          \n" // Parameter 1: pointer address
        "leaq    .LC)" +
                       fnvars.mangledName +
                       R"((%rip), %rsi    \n" // Address of string "a"

        // Call loadfnToPtr function
        "call    loadfnToPtr  \n" // Call loadfnToPtr function

        // Restore all general-purpose registers
        "popq    %r15                \n"
        "popq    %r14                \n"
        "popq    %r13                \n"
        "popq    %r12                \n"
        "popq    %r11                \n"
        "popq    %r10                \n"
        "popq    %r9                 \n"
        "popq    %r8                 \n"
        "popq    %rbp                \n"
        "popq    %rdi                \n"
        "popq    %rsi                \n"
        "popq    %rdx                \n"
        "popq    %rcx                \n"
        "popq    %rbx                \n"
        "popq    %rax                \n");
    __asm__ __volatile__("jmp *%0\n"
                         :
                         : "r"()" +
                       fnvars.mangledName +
                       R"(_ptr));


    __asm__(".section .rodata            \n"
            ".LC)" + fnvars.mangledName +
                       R"(:                        \n"
            ".string \")" +
                       fnvars.mangledName +
                       R"(\"                \n");
    __asm__(".section .text            \n");
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

void fillWrapperPtrs(
    const std::unordered_map<std::string, std::string> &functions,
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

void evalEverything() {
    for (const auto &fn : lazyEvalFns) {
        fn();
    }

    lazyEvalFns.clear();
}

void resolveSymbolOffsetsFromLibraryFile(
    const std::unordered_map<std::string, std::string> &functions) {
    if (functions.empty()) {
        return;
    }

    char command[1024]{};
    sprintf(command, "nm -D --defined-only %s", lastLibrary.c_str());
    FILE *symbol_address_command = popen(command, "r");
    if (symbol_address_command == NULL) {
        perror("popen");
        exit(EXIT_FAILURE);
    }

    char line[1024]{};

    while (fgets(line, 1024, symbol_address_command) != NULL) {
        uintptr_t symbol_offset = 0;
        char symbol_name[256];
        sscanf(line, "%zx %*s %255s", &symbol_offset, symbol_name);

        if (functions.contains(symbol_name)) {
            symbolsToResolve[symbol_name] = symbol_offset;
        }
    }

    pclose(symbol_address_command);
}

auto getBuiltFileDecls(const std::string &path) -> std::vector<VarDecl> {
    std::vector<VarDecl> vars;
    char command[2048]{}, line[MAX_LINE_LENGTH]{};

    // Get the address of the symbol within the library using nm
    snprintf(command, std::size(command), "nm %s | grep ' T '", path.c_str());
    FILE *symbol_address_command = popen(command, "r");
    if (symbol_address_command == NULL) {
        perror("popen");
        exit(EXIT_FAILURE);
    }

    // Parse the output of nm command to get the symbol address
    while (fgets(line, MAX_LINE_LENGTH, symbol_address_command) != NULL &&
           !std::feof(symbol_address_command)) {
        char address[32]{};
        char symbol_location[256]{};
        char symbol_name[512]{};
        sscanf(line, "%16s %s %s", address, symbol_location, symbol_name);
        vars.push_back({.name = symbol_name,
                        .mangledName = symbol_name,
                        .kind = "FunctionDecl"});
    }

    pclose(symbol_address_command);

    return vars;
}

auto prepareWraperAndLoadCodeLib(const CompilerCodeCfg &cfg,
                                 std::vector<VarDecl> &&vars) -> EvalResult {
    std::unordered_map<std::string, std::string> functions;

    prepareFunctionWrapper(cfg.repl_name, vars, functions);

    void *handlewp = nullptr;

    if (!functions.empty()) {
        handlewp = dlopen(("./libwrapper_" + cfg.repl_name + ".so").c_str(),
                          RTLD_NOW | RTLD_GLOBAL);

        if (!handlewp) {
            std::cerr << "Cannot wrapper library: " << dlerror() << '\n';
            // return false;
        }
    }

    int dlOpenFlags = RTLD_NOW | RTLD_GLOBAL;

    if (cfg.lazyEval) {
        std::cout << "lazyEval:  " << cfg.repl_name << std::endl;
        dlOpenFlags = RTLD_LAZY | RTLD_GLOBAL;
    }

    auto load_start = std::chrono::steady_clock::now();

    lastLibrary = ("./lib" + cfg.repl_name + ".so");

    resolveSymbolOffsetsFromLibraryFile(functions);

    EvalResult result;
    result.libpath = lastLibrary;

    void *handle = dlopen(lastLibrary.c_str(), dlOpenFlags);
    result.handle = handle;
    if (!handle) {
        std::cerr << __FILE__ << ":" << __LINE__
                  << " Cannot open library: " << dlerror() << '\n';
        result.success = false;
        return result;
    }

    symbolsToResolve.clear();

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
            } catch (const segvcatch::hardware_exception &e) {
                std::cerr << "Hardware exception: " << e.what() << std::endl;
                std::cerr << assembly_info::getInstructionAndSource(
                                 getpid(),
                                 reinterpret_cast<uintptr_t>(e.info.addr))
                          << std::endl;
                auto [btrace, size] =
                    backtraced_exceptions::get_backtrace_for(e);

                if (btrace != nullptr && size > 0) {
                    std::cerr
                        << "Backtrace (based on callstack return address): "
                        << std::endl;

                    backtraced_exceptions::print_backtrace(btrace, size);
                } else {
                    std::cerr << "Backtrace not available" << std::endl;
                }
            } catch (const std::exception &e) {
                std::cerr << "C++ exception on exec/eval: " << e.what()
                          << std::endl;

                auto [btrace, size] =
                    backtraced_exceptions::get_backtrace_for(e);

                if (btrace != nullptr && size > 0) {
                    std::cerr
                        << "Backtrace (based on callstack return address): "
                        << std::endl;
                    backtraced_exceptions::print_backtrace(btrace, size);
                } else {
                    std::cerr << "Backtrace not available" << std::endl;
                }
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

    if (auto execPtr = (void (*)())dlsym(handle, "_Z4execv");
        execPtr != nullptr) {
        result.exec = execPtr;
    }

    if (cfg.lazyEval) {
        lazyEvalFns.push_back(eval);
    } else {
        eval();
    }

    result.success = true;
    return result;
}

bool loadPrebuilt(const std::string &path) {
    std::unordered_map<std::string, std::string> functions;

    std::vector<VarDecl> vars = getBuiltFileDecls(path);

    auto filename = "lib_" + std::filesystem::path(path).filename().string();

    prepareFunctionWrapper(filename, vars, functions);

    void *handlewp = nullptr;

    if (!functions.empty()) {
        handlewp = dlopen(("./libwrapper_" + filename + ".so").c_str(),
                          RTLD_NOW | RTLD_GLOBAL);

        if (!handlewp) {
            std::cerr << "Cannot wrapper library: " << dlerror() << '\n';
            // return false;
        }
    }

    int dlOpenFlags = RTLD_NOW | RTLD_GLOBAL;

    std::string library = path;

    if (filename.ends_with(".a") || filename.ends_with(".o")) {
        // g++  -Wl,--whole-archive base64.cc.o -Wl,--no-whole-archive -shared
        // -o teste.o
        library = "./" + filename + ".so";
        std::string cmd = "g++ -Wl,--whole-archive " + path +
                          " -Wl,--no-whole-archive -shared -o " + library;
        std::cout << cmd << std::endl;
        system(cmd.c_str());
    }

    auto load_start = std::chrono::steady_clock::now();

    lastLibrary = library;

    resolveSymbolOffsetsFromLibraryFile(functions);

    void *handle = dlopen(lastLibrary.c_str(), dlOpenFlags);
    if (!handle) {
        std::cerr << __FILE__ << ":" << __LINE__
                  << " Cannot open library: " << dlerror() << '\n';
        return false;
    }

    symbolsToResolve.clear();

    auto load_end = std::chrono::steady_clock::now();

    std::cout << "load time: "
              << std::chrono::duration_cast<std::chrono::microseconds>(
                     load_end - load_start)
                     .count()
              << "us" << std::endl;

    fillWrapperPtrs(functions, handlewp, handle);

    return true;
}

auto compileAndRunCode(CompilerCodeCfg &&cfg) -> EvalResult {
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
        shouldRecompilePrecompiledHeader = true;
        return {};
    }

    auto end = std::chrono::steady_clock::now();

    {
        auto vars2 = getBuiltFileDecls("./lib" + cfg.repl_name + ".so");

        // add only if it does not exist
        for (const auto &var : vars2) {
            if (std::find_if(vars.begin(), vars.end(),
                             [&](const VarDecl &varInst) {
                                 return varInst.mangledName == var.mangledName;
                             }) == vars.end()) {
                std::cout << __FILE__ << ":" << __LINE__
                          << " added: " << var.name << std::endl;
                vars.push_back(var);
            }
        }
    }

    std::cout << "build time: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(end -
                                                                       now)
                     .count()
              << "ms" << std::endl;

    return prepareWraperAndLoadCodeLib(cfg, std::move(vars));
}

bool useCpp2 = false;

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

    if (line.starts_with("#loadprebuilt ")) {
        line = line.substr(14);

        loadPrebuilt(line);
        return true;
    }

    if (line.starts_with("#cpp2")) {
        useCpp2 = true;
        return true;
    }

    if (line.starts_with("#cpp1")) {
        useCpp2 = false;
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
    cfg.use_cpp2 = useCpp2;

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

            cfg.use_cpp2 = (textension == "cpp2");

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

        line = "void exec() { printdata(((" + line + ")), " +
               utility::quote(line) + ", typeid(decltype((" + line +
               "))).name()); }\n";

        cfg.analyze = false;
    }

    if (auto rerun = evalResults.find(line); rerun != evalResults.end()) {
        try {
            if (rerun->second.exec) {
                std::cout << "Rerunning compiled command" << std::endl;
                rerun->second.exec();
                return true;
            }
        } catch (const segvcatch::interrupted_by_the_user &e) {
            std::cerr << "Interrupted by the user: " << e.what() << std::endl;
            return true;
        } catch (const segvcatch::hardware_exception &e) {
            std::cerr << "Hardware exception: " << e.what() << std::endl;
            std::cerr << assembly_info::getInstructionAndSource(
                             getpid(), reinterpret_cast<uintptr_t>(e.info.addr))
                      << std::endl;
            auto [btrace, size] = backtraced_exceptions::get_backtrace_for(e);

            if (btrace != nullptr && size > 0) {
                std::cerr << "Backtrace: " << std::endl;

                backtraced_exceptions::print_backtrace(btrace, size);
            } else {
                std::cerr << "Backtrace not available" << std::endl;
            }
        } catch (const std::exception &e) {
            std::cerr << "C++ exception on exec/eval: " << e.what()
                      << std::endl;

            auto [btrace, size] = backtraced_exceptions::get_backtrace_for(e);

            if (btrace != nullptr && size > 0) {
                std::cerr << "Backtrace: " << std::endl;

                backtraced_exceptions::print_backtrace(btrace, size);
            } else {
                std::cerr << "Backtrace not available" << std::endl;
            }
        } catch (...) {
            std::cerr << "Unknown C++ exception on exec/eval" << std::endl;
        }

        return true;
    }

    // std::cout << line << std::endl;

    cfg.repl_name = "repl_" + std::to_string(i++);

    if (cfg.fileWrap) {
        std::fstream replOutput(cfg.repl_name + "." +
                                    (cfg.use_cpp2 ? "cpp2" : cfg.extension),
                                std::ios::out | std::ios::trunc);

        if (cfg.addIncludes) {
            replOutput << "#include \"precompiledheader.hpp\"\n\n";
            replOutput << "#include \"decl_amalgama.hpp\"\n\n";
        }

        replOutput << line << std::endl;

        replOutput.close();
    }

    if (cfg.use_cpp2) {
        int cppfrontres =
            system(("./cppfront " + cfg.repl_name + ".cpp2").c_str());

        if (cppfrontres != 0) {
            std::cerr << "cppfrontres != 0: " << cfg.repl_name << std::endl;
            return false;
        }
    }

    auto evalRes = compileAndRunCode(std::move(cfg));

    if (evalRes.success) {
        evalResults.insert_or_assign(line, evalRes);
    }

    return true;
}

int __attribute__((visibility("default"))) (*bootstrapProgram)(
    int argc, char **argv) = nullptr;

int64_t replCounter = 0;

auto compileAndRunCodeCustom(
    const std::unordered_map<std::string, std::string> &commands,
    const std::vector<std::string> &objects) -> EvalResult {
    auto now = std::chrono::steady_clock::now();

    auto [vars, returnCode] = analyzeCustomCommands(commands);

    if (returnCode != 0) {
        return {};
    }

    CompilerCodeCfg cfg;
    cfg.repl_name = "custom_lib_" + std::to_string(replCounter++);

    returnCode = linkAllObjects(objects, cfg.repl_name);

    if (returnCode != 0) {
        return {};
    }

    mergeVars(vars);

    auto end = std::chrono::steady_clock::now();

    std::cout << "build time: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(end -
                                                                       now)
                     .count()
              << "ms" << std::endl;

    return prepareWraperAndLoadCodeLib(cfg, std::move(vars));
}

int ctrlcounter = 0;

void handleCtrlC(const segvcatch::hardware_exception_info &info) {
    std::cout << "Ctrl-C pressed" << std::endl;

    if (ctrlcounter == 0) {
        std::cout << "Press Ctrl-C again to exit" << std::endl;
        ctrlcounter++;
    }

    throw segvcatch::interrupted_by_the_user("Ctrl-C pressed", info);
}

void installCtrlCHandler() { segvcatch::init_ctrlc(&handleCtrlC); }

auto extExecRepl(std::string_view lineview) -> bool {
    return execRepl(lineview, replCounter);
}

void repl() {
    std::string line;

    std::string_view historyFile = "history.txt";
    if (read_history(historyFile.data()) != 0) {
        perror("Failed to read history");
    }

    while (true) {
        try {
            char *input = readline(">>> ");

            if (input == nullptr) {
                break;
            }

            line = input;
            free(input);
        } catch (const segvcatch::interrupted_by_the_user &e) {
            std::cout << "Ctrl-C pressed" << std::endl;
            break;
        }

        ctrlcounter = 0;
        add_history(line.c_str());

        try {
            if (!extExecRepl(line)) {
                break;
            }
        } catch (const segvcatch::segmentation_fault &e) {
            std::cerr << "Segmentation fault: " << e.what() << std::endl;
            std::cerr << assembly_info::getInstructionAndSource(
                             getpid(), reinterpret_cast<uintptr_t>(e.info.addr))
                      << std::endl;
        } catch (const segvcatch::floating_point_error &e) {
            std::cerr << "Floating point error: " << e.what() << std::endl;
            std::cerr << assembly_info::getInstructionAndSource(
                             getpid(), reinterpret_cast<uintptr_t>(e.info.addr))
                      << std::endl;
        } catch (const std::exception &e) {
            std::cerr << "C++ Exception: " << e.what() << std::endl;
        }

        if (bootstrapProgram) {
            break;
        }
    }

    if (write_history(historyFile.data()) != 0) {
        perror("Failed to write history");
    }
}

int ext_build_precompiledheader() { return build_precompiledheader(); }

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

void initNotifications(std::string_view appName) {
#ifndef NUSELIBNOTIFY
    notify_init(appName.data());
#else
    std::cerr << "Notifications not available" << std::endl;
#endif
}

void notifyError(std::string_view summary, std::string_view msg) {
#ifndef NUSELIBNOTIFY
    NotifyNotification *notification = notify_notification_new(
        summary.data(), msg.data(), "/home/fabio/Downloads/icons8-erro-64.png");

    // Set the urgency level of the notification
    notify_notification_set_urgency(notification, NOTIFY_URGENCY_CRITICAL);

    // Show the notification
    notify_notification_show(notification, NULL);
#else
    std::cerr << summary << ": " << msg << std::endl;
#endif
}

std::any getResultRepl(std::string cmd) {
    lastReplResult = std::any();

    cmd.insert(0, "#eval lastReplResult = (");
    cmd += ");";

    execRepl(cmd, replCounter);

    return lastReplResult;
}
