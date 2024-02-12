#include "simdjson.h"
#include <chrono>
#include <cstdlib>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

/*int main() {
    void *handle = dlopen("./libone.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!handle) {
        std::cerr << "Cannot open library: " << dlerror() << '\n';
        return 1;
    }

    void (*printdata)() = (void (*)())dlsym(handle, "_Z9printdatav");
    if (!printdata) {
        std::cerr << "Cannot load symbol 'printdata': " << dlerror() << '\n';
        dlclose(handle);
        return 1;
    }

    printdata();

    void *handletwo = dlopen("./libtwo.so", RTLD_LAZY | RTLD_GLOBAL);

    if (!handletwo) {
        std::cerr << "Cannot open library: " << dlerror() << '\n';
        return 1;
    }

    void (*printdata2)() = (void (*)())dlsym(handletwo, "_Z9printdatav");

    if (!printdata2) {
        std::cerr << "Cannot load symbol 'printdata': " << dlerror() << '\n';
        dlclose(handletwo);
        return 1;
    }

    printdata2();

    dlclose(handle);
}*/

std::unordered_set<std::string> linkLibraries;

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

    for (const auto &lib : linkLibraries) {
        linkLibrariesStr += " -l" + lib;
    }

    return linkLibrariesStr;
}

void onlybuild(const std::string &name) {
    auto cmd = "clang++ -std=c++20 -shared -include precompiledheader.hpp -g "
               "-Wl,--export-dynamic -fPIC " +
               name + ".cpp " + getLinkLibrariesStr() +
               " -o "
               "lib" +
               name + ".so";

    system(cmd.c_str());
}

void build(const std::string &name) {
    auto cmd = "clang++ -std=c++20 -shared -include precompiledheader.hpp -g "
               "-Wl,--export-dynamic -fPIC " +
               name + ".cpp " + getLinkLibrariesStr() +
               " -o "
               "lib" +
               name + ".so > " + name + ".json";
    system(cmd.c_str());
    cmd = "clang++ -std=c++20 -fPIC -Xclang -ast-dump=json -include "
          "precompiledheader.hpp -fsyntax-only " +
          name +
          ".cpp -o "
          "lib" +
          name + ".so > " + name + ".json";
    system(cmd.c_str());
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
    /*std::cout << "if valid, the document has the following type at the root: "
              << type << std::endl;*/
    auto inner = doc["inner"];

    std::string lastfile;

    if (inner.error() == simdjson::SUCCESS) {
        // std::cout << "inner is an object" << std::endl;
        auto inner_type = inner.type();
        if (inner_type.error() == simdjson::SUCCESS) {
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
                    auto includedFrom_string =
                        includedFrom["file"].get_string();

                    if (!includedFrom_string.error()) {
                        std::filesystem::path p(lastfile);

                        p = std::filesystem::absolute(
                            std::filesystem::canonical(p));

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

                if (lastfile != source) {
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
                    storageClassJs.error()
                        ? ""
                        : storageClassJs.get_string().value());

                if (storageClass == "extern" || storageClass == "static") {
                    continue;
                }

                if (kind_string.value() == "FunctionDecl") {
                    auto qualTypestr = std::string(qualType_string.value());

                    auto parem = qualTypestr.find_first_of('(');

                    if (parem == std::string::npos) {
                        continue;
                    }

                    qualTypestr.insert(parem, std::string(name_string.value()));

                    std::cout << "extern " << qualTypestr << ";" << std::endl;

                    outputHeader += "extern " + qualTypestr + ";\n";

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
                    outputHeader += "#line " +
                                    std::to_string(lline_int.value()) + " \"" +
                                    lastfile + "\"\n";
                    outputHeader += "extern " +
                                    std::string(qualType_string.value()) + " " +
                                    std::string(name_string.value()) + ";\n";

                    VarDecl var;

                    auto type_var = type["desugaredQualType"];

                    var.name = name_string.value();
                    var.type =
                        type_var.error() ? "" : type_var.get_string().value();
                    var.qualType = qualType_string.value();
                    var.kind = kind_string.value();
                    var.file = lastfile;
                    var.line = lline_int.value();

                    vars.push_back(std::move(var));
                }
            }
        } else {
            std::cout << "inner is not an object" << std::endl;
        }
    } else {
        std::cout << "inner is not an object" << std::endl;
    }

    // std::cout << "lastfile: " << lastfile << std::endl;
    // std::cout << outputHeader << std::endl;

    std::fstream headerOutput("decl_amalgama.hpp", std::ios::out);
    headerOutput << outputHeader << std::endl;
    headerOutput.close();

    return EXIT_SUCCESS;
}

void writeHeaderPrintOverloads() {
    auto printer = R"cpp(
#pragma once
#include <iostream>
#include <vector>
#include <string_view>

template <class T>
void printdata(const std::vector<T> &vect, std::string_view name) {
    std::cout << " >> " << typeid(T).name() << (name.empty()? "" : " ") << (name.empty()? "" : name) << ": ";
    for (const auto &v : vect) {
        std::cout << v << ' ';
    }

    std::cout << std::endl;
}

void printdata(std::string_view str, std::string_view name) {
    std::cout << " >> " << (name.empty()? "" : " ") << (name.empty()? "" : name) << str << std::endl;
}

template <class T>
void printdata(const T &val, std::string_view name) {
    std::cout << " >> " << typeid(T).name() << (name.empty()? "" : " ") << (name.empty()? "" : name) << ": " << val << std::endl;
}

    )cpp";

    std::fstream printerOutput("printerOutput.hpp",
                               std::ios::out | std::ios::trunc);

    printerOutput << printer << std::endl;

    printerOutput.close();
}

void build_precompiledheader() {
    std::fstream printerOutput("precompiledheader.hpp",
                               std::ios::out | std::ios::trunc);

    printerOutput << "#pragma once\n\n" << std::endl;
    printerOutput << "#include \"printerOutput.hpp\"\n\n" << std::endl;

    for (const auto &include : includedFiles) {
        if (std::filesystem::exists(include)) {
            printerOutput << "#include \"" << include << "\"\n";
        } else {
            printerOutput << "#include <" << include << ">\n";
        }
    }

    printerOutput.flush();
    printerOutput.close();

    std::string cmd = "clang++ -fPIC -x c++-header -std=c++20 -o "
                      "precompiledheader.hpp.pch precompiledheader.hpp";
    system(cmd.c_str());
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

    onlybuild(name);

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

    onlybuild("printerOutput");
}

struct wrapperFn {
    void *fnptr;
    void **wrap_ptrfn;
};

std::unordered_set<std::string> varsNames;
std::unordered_map<std::string, wrapperFn> fnNames;
std::vector<VarDecl> todasVars;

auto build_wprint(const std::string &name) -> std::vector<VarDecl> {
    auto cmd = "clang++ -std=c++20 -fPIC -Xclang -ast-dump=json -include "
               "precompiledheader.hpp -fsyntax-only " +
               name + ".cpp " + getLinkLibrariesStr() +
               " -o "
               "lib" +
               name + ".so > " + name + ".json";
    int analyzeres = system(cmd.c_str());

    if (analyzeres != 0) {
        return {};
    }

    std::vector<VarDecl> vars;

    analyzeast(name + ".json", name + ".cpp", vars);

    cmd = "clang++ -std=c++20 -shared -include precompiledheader.hpp -g "
          "-Wl,--export-dynamic -fPIC " +
          name + ".cpp " + getLinkLibrariesStr() +
          " -o "
          "lib" +
          name + ".so > " + name + ".json";
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

auto build_wnprint(const std::string &name) -> std::vector<VarDecl> {
    auto cmd = "clang++ -std=c++20 -fPIC -Xclang -ast-dump=json -include "
               "precompiledheader.hpp -fsyntax-only " +
               name + ".cpp " + getLinkLibrariesStr() +
               " -o "
               "lib" +
               name + ".so > " + name + ".json";
    int analyzeres = system(cmd.c_str());

    if (analyzeres != 0) {
        return {};
    }

    std::vector<VarDecl> vars;

    analyzeast(name + ".json", name + ".cpp", vars);

    cmd = "clang++ -std=c++20 -shared -include precompiledheader.hpp -g "
          "-Wl,--export-dynamic -fPIC " +
          name + ".cpp " + getLinkLibrariesStr() +
          " -o "
          "lib" +
          name + ".so";
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
        if (fnvars.kind != "FunctionDecl") {
            continue;
        }

        if (!fnNames.contains(fnvars.mangledName)) {
            auto qualTypestr = std::string(fnvars.qualType);
            auto parem = qualTypestr.find_first_of('(');

            if (parem == std::string::npos) {
                qualTypestr =
                    "void __attribute__ ((naked)) " + fnvars.name + "()";
            } else {
                qualTypestr.insert(
                    parem,
                    std::string(" __attribute__ ((naked)) " + fnvars.name));
            }

            wrapper +=
                "extern \"C\" void *" + fnvars.mangledName + "_ptr = nullptr;\n\n";
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

        onlybuild(wrappername);
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

        void **wrap_ptrfn = (void **)dlsym(handlewp, (mangledName + "_ptr").c_str());

        if (!wrap_ptrfn) {
            std::cerr << "Cannot load symbol '" << mangledName << "_ptr': " << dlerror()
                      << '\n';
            continue;
        }

        *wrap_ptrfn = fnptr;
        fn.wrap_ptrfn = wrap_ptrfn;
    }
}

auto execRepl(std::string_view lineview, int64_t &i) -> bool {
    std::string line(lineview);
    if (line == "exit") {
        return false;
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

                    build_precompiledheader();
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

    if (line == "printall") {
        std::cout << "printall" << std::endl;

        printAllSave(todasVars);
        runPrintAll();
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

        onlybuild("printerOutput");

        runPrintAll();

        return true;
    }

    bool analyze = true;

    if (line.starts_with("#eval ")) {
        line = line.substr(6);

        if (std::filesystem::exists(line)) {
            std::fstream file(line, std::ios::in);

            line.clear();
            std::copy(std::istreambuf_iterator<char>(file),
                      std::istreambuf_iterator<char>(),
                      std::back_inserter(line));
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

    std::cout << line << std::endl;

    std::string name = "repl_" + std::to_string(i++);

    std::fstream replOutput(name + ".cpp", std::ios::out | std::ios::trunc);

    replOutput << "#include \"precompiledheader.hpp\"\n\n";
    replOutput << "#include \"decl_amalgama.hpp\"\n\n";

    replOutput << line << std::endl;

    replOutput.close();

    std::vector<VarDecl> vars;

    if (analyze) {
        vars = build_wnprint(name);
    } else {
        onlybuild(name);
    }

    std::unordered_map<std::string, std::string> functions;

    prepareFunctionWrapper(name, vars, functions);

    void *handlewp = nullptr;

    if (!functions.empty()) {
        handlewp = dlopen(("./libwrapper_" + name + ".so").c_str(),
                          RTLD_NOW | RTLD_GLOBAL);
    }

    auto load_start = std::chrono::steady_clock::now();

    void *handle =
        dlopen(("./lib" + name + ".so").c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        std::cerr << "Cannot open library: " << dlerror() << '\n';
        return true;
    }

    auto load_end = std::chrono::steady_clock::now();

    std::cout << "load time: "
              << std::chrono::duration_cast<std::chrono::microseconds>(
                     load_end - load_start)
                     .count()
              << "us" << std::endl;

    fillWrapperPtrs(functions, handlewp, handle);

    printPrepareAllSave(vars);

    void (*execv)() = (void (*)())dlsym(handle, "_Z4execv");
    if (execv) {
        auto exec_start = std::chrono::steady_clock::now();
        execv();
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
            return true;
        } else {
            std::cout << "not found" << std::endl;
        }
    }

    std::cout << std::endl;
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

    writeHeaderPrintOverloads();
    build_precompiledheader();

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

int main(int argc, char **argv) {
    outputHeader.reserve(1024 * 1024);
    outputHeader += "#pragma once\n";
    outputHeader += "#include \"precompiledheader.hpp\"\n";
    outputHeader += "extern int (*bootstrapProgram)(int argc, char "
                    "**argv);\n";

    if (!std::filesystem::exists("decl_amalgama.hpp")) {
        std::fstream printerOutput("decl_amalgama.hpp",
                                   std::ios::out | std::ios::trunc);
        printerOutput << outputHeader << std::endl;
        printerOutput.close();
    }

    repl();

    if (bootstrapProgram) {
        return bootstrapProgram(argc, argv);
    }

    /*std::vector<VarDecl> vars;

    std::string name = "one";
    build_wprint(name);

    name = "two";
    build(name);
    analyzeast(name + ".json", name + ".cpp", vars);

    void *handle = dlopen("./libone.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!handle) {
        std::cerr << "Cannot open library: " << dlerror() << '\n';
        return 1;
    }

    void (*printdata)() = (void (*)())dlsym(handle, "_Z9printdatav");
    if (!printdata) {
        std::cerr << "Cannot load symbol 'printdata': " << dlerror() << '\n';
        dlclose(handle);
        return 1;
    }

    void *handlep = dlopen("./libprinterOutput.so", RTLD_NOW | RTLD_GLOBAL);
    if (!handlep) {
        std::cerr << "Cannot open library: " << dlerror() << '\n';
        return 1;
    }

    void (*printall)() = (void (*)())dlsym(handlep, "_Z8printallv");
    if (!printall) {
        std::cerr << "Cannot load symbol 'printall': " << dlerror() << '\n';
        dlclose(handlep);
        return 1;
    }

    printall();

    printdata();

    void *handletwo = dlopen("./libtwo.so", RTLD_LAZY | RTLD_GLOBAL);

    if (!handletwo) {
        std::cerr << "Cannot open library: " << dlerror() << '\n';
        return 1;
    }

    void (*printdata2)() = (void (*)())dlsym(handletwo, "_Z9printdatav");

    if (!printdata2) {
        std::cerr << "Cannot load symbol 'printdata': " << dlerror() << '\n';
        dlclose(handletwo);
        return 1;
    }

    printdata2();

    dlclose(handle);*/
}
