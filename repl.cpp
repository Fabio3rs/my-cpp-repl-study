#include "stdafx.hpp"

#include "repl.hpp"
#include "simdjson.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <dlfcn.h>
#include <filesystem>
#include <functional>
#include <string>

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

    std::cout << "includeDirectoriesStr: " << includeDirectoriesStr
              << std::endl;

    return includeDirectoriesStr;
}

auto getPreprocessorDefinitionsStr() -> std::string {
    std::string preprocessorDefinitionsStr;

    for (const auto &def : preprocessorDefinitions) {
        preprocessorDefinitionsStr += " -D" + def;
    }

    return preprocessorDefinitionsStr;
}

int onlybuild(std::string compiler, const std::string &name,
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

int build(std::string compiler, const std::string &name,
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
               name + ".so > " + name + ".json";
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

void analyzeInner(const std::string &source, std::vector<VarDecl> &vars,
                  simdjson::simdjson_result<simdjson::ondemand::value> inner) {
    std::string lastfile;

    if (inner.error() != simdjson::SUCCESS) {
        std::cout << "inner is not an object" << std::endl;
        return;
    }

    // std::cout << "inner is an object" << std::endl;
    auto inner_type = inner.type();
    if (inner_type.error() != simdjson::SUCCESS ||
        inner_type.value() != simdjson::ondemand::json_type::array) {
        std::cout << "inner is not an array" << std::endl;
    }

    /*std::cout << "inner is an array " << inner_type.value()
                  << std::endl;*/

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
                // std::cout << "file: " << lastfile << std::endl;
            }
        }

        auto includedFrom = loc["includedFrom"];

        if (!includedFrom.error()) {
            auto includedFrom_string = includedFrom["file"].get_string();

            if (!includedFrom_string.error()) {
                std::filesystem::path p(lastfile);

                p = std::filesystem::absolute(std::filesystem::canonical(p));

                std::string path = p.string();

                /*std::cout << "includedFrom: "
                          << includedFrom_string.value() << path <<
                   std::endl;*/

                if (p.filename() != "decl_amalgama.hpp" &&
                    p.filename() != "printerOutput.hpp" &&
                    includedFrom_string.value() == source &&
                    includedFiles.find(path) == includedFiles.end()) {
                    includedFiles.insert(path);
                    outputHeader += "#include \"" + path + "\"\n";
                }
            }
        }

        if (!source.empty() && !lastfile.empty() && lastfile != source) {
            continue;
        }

        auto lline = loc["line"];

        if (lline.error()) {
            continue;
        }

        auto lline_int = lline.get_int64();

        if (lline_int.error()) {
            continue;
        }

        // std::cout << "line: " << lline_int.value() << std::endl;

        auto kind = element["kind"];

        if (kind.error()) {
            continue;
        }

        auto kind_string = kind.get_string();

        if (kind_string.error()) {
            continue;
        }

        // std::cout << "kind: " << kind_string.value() << std::endl;

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
            analyzeInner(source, vars, element["inner"]);
            continue;
        }

        // std::cout << "name: " << name_string.value() << std::endl;

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

        /*std::cout << "qualType: " << qualType_string.value()
                  << std::endl;*/

        auto storageClassJs = element["storageClass"];

        std::string storageClass(
            storageClassJs.error() ? "" : storageClassJs.get_string().value());

        if (storageClass == "extern" || storageClass == "static") {
            continue;
        }

        if (kind_string.value() == "FunctionDecl" ||
            kind_string.value() == "CXXMethodDecl") {

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
            outputHeader += "#line " + std::to_string(lline_int.value()) +
                            " \"" + lastfile + "\"\n";

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

int analyzeast(const std::string &filename, const std::string &source,
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
    simdjson::ondemand::parser parser;
    simdjson::ondemand::document doc;
    error = parser.iterate(json).get(doc);
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

    /*std::cout << "if valid, the document has the following type at the root: "
              << type << std::endl;*/
    auto inner = doc["inner"];

    analyzeInner(source, vars, inner);

    // std::cout << "lastfile: " << lastfile << std::endl;
    // std::cout << outputHeader << std::endl;

    if (outputHeaderSize != outputHeader.size()) {
        std::fstream headerOutput("decl_amalgama.hpp", std::ios::out);
        headerOutput << outputHeader << std::endl;
        headerOutput.close();
    }

    return EXIT_SUCCESS;
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
inline void printdata(const std::vector<T> &vect, std::string_view name) {
    std::cout << " >> " << typeid(T).name() << (name.empty()? "" : " ") << (name.empty()? "" : name) << ": ";
    for (const auto &v : vect) {
        std::cout << v << ' ';
    }

    std::cout << std::endl;
}

template <class T>
inline void printdata(const std::deque<T> &vect, std::string_view name) {
    std::cout << " >> " << typeid(T).name() << (name.empty()? "" : " ") << (name.empty()? "" : name) << ": ";
    for (const auto &v : vect) {
        std::cout << v << ' ';
    }

    std::cout << std::endl;
}

inline void printdata(std::string_view str, std::string_view name) {
    std::cout << " >> " << (name.empty()? "" : " ") << (name.empty()? "" : name) << str << std::endl;
}

inline void printdata(const std::mutex &mtx, std::string_view name) {
    std::cout << " >> " << (name.empty()? "" : " ") << (name.empty()? "" : name) << "Mutex" << std::endl;
}

template <class T>
inline void printdata(const T &val, std::string_view name) {
    std::cout << " >> " << typeid(T).name() << (name.empty()? "" : " ") << (name.empty()? "" : name) << ": " << val << std::endl;
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
        if (var.kind == "VarDecl") {
            printerOutput << "extern \"C\" void printvar_" << var.name
                          << "() {\n";
            printerOutput << "  printdata(" << var.name << ", \"" << var.name
                          << "\");\n";
            printerOutput << "}\n";
        }
    }

    printerOutput << "void printall() {\n";

    for (const auto &var : vars) {
        // std::cout << __LINE__ << var.kind << std::endl;
        if (var.kind == "VarDecl") {
            printerOutput << "printdata(" << var.name << ", \"" << var.name
                          << "\");\n";
        }
    }

    printerOutput << "}\n";

    printerOutput.close();

    onlybuild("clang++", name);

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

void printAllSave(const std::vector<VarDecl> &vars) {
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
                          << "\");\n";
        }
    }

    printerOutput << "}\n";

    printerOutput.close();

    onlybuild("clang++", "printerOutput");
}

struct wrapperFn {
    void *fnptr;
    void **wrap_ptrfn;
};

std::unordered_set<std::string> varsNames;
std::unordered_map<std::string, wrapperFn> fnNames;
std::vector<VarDecl> todasVars;

auto build_wprint(std::string compiler, const std::string &name)
    -> std::vector<VarDecl> {
    auto cmd = compiler + getPreprocessorDefinitionsStr() + " " +
               getIncludeDirectoriesStr() +
               " -std=c++20 -fPIC -Xclang -ast-dump=json -include "
               "precompiledheader.hpp -fsyntax-only " +
               name + ".cpp " + getLinkLibrariesStr() +
               " -o "
               "lib" +
               name + ".so > " + name + ".json";

    std::cout << cmd << std::endl;
    int analyzeres = system(cmd.c_str());

    if (analyzeres != 0) {
        return {};
    }

    std::vector<VarDecl> vars;

    analyzeast(name + ".json", name + ".cpp", vars);

    cmd = compiler + getPreprocessorDefinitionsStr() + " " +
          getIncludeDirectoriesStr() +
          " -std=c++20 -shared -include precompiledheader.hpp -g "
          "-Wl,--export-dynamic -fPIC " +
          name + ".cpp " + getLinkLibrariesStr() +
          " -o "
          "lib" +
          name + ".so > " + name + ".json";
    std::cout << cmd << std::endl;
    system(cmd.c_str());

    printAllSave(vars);

    // merge todasVars with vars
    for (const auto &var : vars) {
        if (varsNames.find(var.name) == varsNames.end()) {
            varsNames.insert(var.name);
            todasVars.push_back(var);
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

auto build_wnprint(std::string compiler, const std::string &libname,
                   const std::vector<std::string> &names)
    -> std::vector<VarDecl> {
    auto namesConcated = concatNames(names);
    std::string cmd;
    std::vector<VarDecl> vars;

    for (const auto &name : names) {
        if (name.empty()) {
            continue;
        }

        auto path = std::filesystem::path(name);
        std::string purefilename = path.filename().string();
        std::string wrappedname = "compiler.cpp";
        {
            std::fstream replOutput(wrappedname,
                                    std::ios::out | std::ios::trunc);

            replOutput << "#include \"precompiledheader.hpp\"\n\n";
            replOutput << "#include \"decl_amalgama.hpp\"\n\n";

            replOutput << "#include \"" << name << "\"" << std::endl;

            replOutput.close();
        }
        std::string jsonname = purefilename + ".json";
        cmd.clear();
        cmd += compiler + getPreprocessorDefinitionsStr() + " " +
               getIncludeDirectoriesStr() +
               " -std=c++20 -fPIC -Xclang -ast-dump=json -include "
               "precompiledheader.hpp -fsyntax-only " +
               wrappedname + " " + getLinkLibrariesStr() +
               " -o "
               "lib" +
               libname + ".so > " + jsonname;
        std::cout << cmd << std::endl;
        int analyzeres = system(cmd.c_str());

        if (analyzeres != 0) {
            return {};
        }

        std::cout << __FILE__ << "    " << name << "     " << name.size()
                  << std::endl;

        analyzeast(jsonname, name, vars);
    }

    cmd = compiler + getPreprocessorDefinitionsStr() + " " +
          getIncludeDirectoriesStr() +
          " -std=c++20 -shared -include precompiledheader.hpp -g "
          "-Wl,--export-dynamic -fPIC " +
          namesConcated + " " + getLinkLibrariesStr() +
          " -o "
          "lib" +
          libname + ".so";
    std::cout << cmd << std::endl;
    system(cmd.c_str());

    // merge todasVars with vars
    for (const auto &var : vars) {
        if (varsNames.find(var.name) == varsNames.end()) {
            varsNames.insert(var.name);
            todasVars.push_back(var);
        }
    }

    return vars;
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

    for (const auto &fnvars : vars) {
        std::cout << fnvars.name << std::endl;
        if (fnvars.kind != "FunctionDecl" && fnvars.kind != "CXXMethodDecl") {
            continue;
        }

        if (fnvars.mangledName == "main") {
            continue;
        }

        if (!fnNames.contains(fnvars.mangledName)) {
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

        onlybuild("clang++", wrappername);
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

void evalEverything() {
    for (const auto &fn : lazyEvalFns) {
        fn();
    }

    lazyEvalFns.clear();
}

auto execRepl(std::string_view lineview, int64_t &i) -> bool {
    std::string line(lineview);
    if (line == "exit") {
        return false;
    }

    if (line.starts_with("#includedir ")) {
        line = line.substr(12);

        includeDirectories.insert(line);

        /*std::fstream file("includeDirectories.txt", std::ios::out);

        for (const auto &dir : includeDirectories) {
            file << dir << std::endl;
        }

        file.close();*/

        return true;
    }

    if (line.starts_with("#compilerdefine ")) {
        line = line.substr(16);

        preprocessorDefinitions.insert(line);

        /*std::fstream file("preprocessorDefinitions.txt", std::ios::out);

        for (const auto &def : preprocessorDefinitions) {
            file << def << std::endl;
        }

        file.close();*/

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
                    p.filename() != "printerOutput.hpp" &&
                    includedFiles.find(path) == includedFiles.end()) {
                    includedFiles.insert(path);

                    shouldRecompilePrecompiledHeader = true;
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

        printAllSave(todasVars);
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

        /*std::fstream file("linkLibraries.txt", std::ios::out);

        for (const auto &lib : linkLibraries) {
            file << lib << std::endl;
        }

        file.close();*/

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

        onlybuild("clang++", "printerOutput");

        runPrintAll();

        return true;
    }

    bool analyze = true;
    bool addIncludes = true;
    bool fileWrap = true;

    std::string compiler = "clang++";
    std::string std = "c++20";
    std::string extension = "cpp";

    bool lazyEval = line.starts_with("#lazyeval ");

    std::vector<std::string> filesList;

    if (line.starts_with("#batch_eval ")) {
        line = line.substr(11);

        std::string file;
        std::istringstream iss(line);
        while (std::getline(iss, file, ' ')) {
            filesList.push_back(file);
        }

        addIncludes = false;
        fileWrap = false;
    }

    if (line.starts_with("#eval ") || line.starts_with("#lazyeval ")) {
        line = line.substr(line.find_first_of(' ') + 1);
        line = line.substr(0, line.find_last_not_of(" \t\n\v\f\r\0") + 1);

        if (std::filesystem::exists(line)) {
            fileWrap = false;

            filesList.push_back(line);
            auto textension = line.substr(line.find_last_of('.') + 1);
            /*std::fstream file(line, std::ios::in);


            line.clear();
            std::copy(std::istreambuf_iterator<char>(file),
                      std::istreambuf_iterator<char>(),
                      std::back_inserter(line));*/

            std::cout << "extension: " << textension << std::endl;

            if (textension == "h" || textension == "hpp") {
                addIncludes = false;
            }

            if (textension == "c") {
                extension = textension;
                analyze = false;
                addIncludes = false;

                std = "c17";
                compiler = "clang";
            }
        } else {
            line = "void exec() { " + line + "; }\n";
            analyze = false;
        }
    }

    if (line.starts_with("#return ")) {
        line = line.substr(8);

        line = "void exec() { printdata(((" + line + ")), \"custom\"); }\n";

        analyze = false;
    }

    // std::cout << line << std::endl;

    std::string name = "repl_" + std::to_string(i++);

    if (fileWrap) {
        std::fstream replOutput(name + "." + extension,
                                std::ios::out | std::ios::trunc);

        if (addIncludes) {
            replOutput << "#include \"precompiledheader.hpp\"\n\n";
            replOutput << "#include \"decl_amalgama.hpp\"\n\n";
        }

        replOutput << line << std::endl;

        replOutput.close();
    }

    std::vector<VarDecl> vars;

    if (filesList.empty()) {
        if (analyze) {
            vars = build_wnprint(compiler, name, {name + ".cpp"});
        } else {
            onlybuild(compiler, name, "." + extension, std);
        }
    } else {
        vars = build_wnprint(compiler, name, filesList);
    }

    std::unordered_map<std::string, std::string> functions;

    prepareFunctionWrapper(name, vars, functions);

    void *handlewp = nullptr;

    if (!functions.empty()) {
        handlewp = dlopen(("./libwrapper_" + name + ".so").c_str(),
                          RTLD_NOW | RTLD_GLOBAL);
    }

    int dlOpenFlags = RTLD_NOW | RTLD_GLOBAL;

    if (lazyEval) {
        std::cout << "lazyEval:  " << name << std::endl;
        dlOpenFlags = RTLD_LAZY | RTLD_GLOBAL;
    }

    auto load_start = std::chrono::steady_clock::now();

    void *handle = dlopen(("./lib" + name + ".so").c_str(), dlOpenFlags);
    if (!handle) {
        std::cerr << __FILE__ << ":" << __LINE__
                  << " Cannot open library: " << dlerror() << '\n';
        return true;
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
        if (execv) {
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

    if (lazyEval) {
        lazyEvalFns.push_back(eval);
    } else {
        eval();
    }

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
