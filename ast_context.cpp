#include "include/analysis/ast_context.hpp"

#include "repl.hpp"
#include "simdjson.h"
#include <cassert>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <mutex>
#include <system_error>

namespace analysis {

// Thread-safe mutex para operações de escrita
static std::mutex contextWriteMutex;

/**
 * @brief duração estática porque as declarações devem ser visíveis em toda a
 * duração do REPL.
 */
std::string AstContext::outputHeader_;

void AstContext::addInclude(const std::string &includePath) {
    std::scoped_lock<std::mutex> lock(contextWriteMutex);
    if (includedFiles_.find(includePath) == includedFiles_.end()) {
        includedFiles_.insert(includePath);
        outputHeader_ += std::format("#include \"{}\"\n", includePath);
    }
}

void AstContext::addDeclaration(const std::string &declaration) {
    std::scoped_lock<std::mutex> lock(contextWriteMutex);
    outputHeader_ += std::format("{}\n", declaration);
}

void AstContext::addLineDirective(int64_t line,
                                  const std::filesystem::path &file) {
    std::scoped_lock<std::mutex> lock(contextWriteMutex);
    outputHeader_ += std::format("#line {} \"{}\"\n", line, file.string());
}

bool AstContext::isFileIncluded(const std::string &filePath) const {
    return includedFiles_.find(filePath) != includedFiles_.end();
}

void AstContext::markFileIncluded(const std::string &filePath) {
    std::scoped_lock<std::mutex> lock(contextWriteMutex);
    includedFiles_.insert(filePath);
}

bool AstContext::hasHeaderChanged() const {
    std::scoped_lock<std::mutex> lock(contextWriteMutex);
    bool changed = outputHeader_.size() != lastHeaderSize_;
    lastHeaderSize_ = outputHeader_.size();
    return changed;
}

void AstContext::clear() {
    std::scoped_lock<std::mutex> lock(contextWriteMutex);

    // CRÍTICO: outputHeader_.clear() NÃO deve ser chamado!
    // O outputHeader_ deve manter duração estática para acumular
    // todas as declarações durante toda a sessão REPL.
    // Limpar esta variável quebra a geração do decl_amalgama.hpp
    // outputHeader_.clear(); // ← NUNCA DESCOMENTE ESTA LINHA!

    includedFiles_.clear();
    lastHeaderSize_ = 0;
}

bool AstContext::saveHeaderToFile(const std::string &filename) const {
    std::fstream headerOutput(filename, std::ios::out);
    if (!headerOutput.is_open()) {
        return false;
    }
    headerOutput << outputHeader_ << std::endl;
    return headerOutput.good();
}

ContextualAstAnalyzer::ContextualAstAnalyzer(
    std::shared_ptr<AstContext> context)
    : context_(context) {
    if (!context_) {
        context_ = std::make_shared<AstContext>();
    }
}

void ContextualAstAnalyzer::analyzeInnerAST(std::filesystem::path source,
                                            std::vector<VarDecl> &vars,
                                            void *innerPtr) {

    // Cast de volta para o tipo correto do simdjson
    auto &inner =
        *static_cast<simdjson::simdjson_result<simdjson::ondemand::value> *>(
            innerPtr);

    std::filesystem::path lastfile;

    if (inner.error() != simdjson::SUCCESS) {
        std::cout << "inner is not an object" << std::endl;
        return;
    }

    auto inner_type = inner.type();
    if (inner_type.error() != simdjson::SUCCESS ||
        inner_type.value() != simdjson::ondemand::json_type::array) {
        std::cout << "inner is not an array" << std::endl;
        return;
    }

    auto inner_array = inner.get_array();

    int64_t lastLine = 0;
    int64_t lastColumn = 0;

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
                    !context_->isFileIncluded(path)) {
                    context_->addInclude(path);
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

        try {
            lastColumn = loc["col"].value();
        } catch (...) {
            lastColumn = 0; // Default to 0 if column is not available
        }

        auto lline = loc["line"];

        if (lline.error()) {
            auto spellingLoc = loc["spellingLoc"];

            if (spellingLoc.error()) {
                if (lastLine <= 0) {
                    continue; // Skip if no valid line information
                }
            } else {
                lline = spellingLoc["line"];

                if (lline.error()) {
                    continue;
                }

                loc = spellingLoc;
            }
        }

        auto lline_int = lline.get_int64();

        if (lline_int.error()) {
            if (lastLine <= 0) {
                continue; // Skip if no valid line information
            }

            // Keep the last line if parsing fails
        } else {
            lastLine = lline_int.value();
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
            auto innerElement = element["inner"];
            analyzeInnerAST(source, vars, &innerElement);
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

            if (kind_string.value() != "CXXMethodDecl") {
                auto qualTypestr = std::string(qualType_string.value());

                auto parem = qualTypestr.find_first_of('(');

                if (parem == std::string::npos) {
                    continue;
                }

                qualTypestr.insert(parem, std::string(name_string.value()));

                std::cout << "extern " << qualTypestr << ";" << std::endl;

                context_->addDeclaration(
                    std::format("extern {};", qualTypestr));
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
            var.line = lastLine;
            var.mangledName = mangledName.get_string().value();

            vars.push_back(std::move(var));
        } else if (kind_string.value() == "VarDecl") {
            context_->addLineDirective(lastLine, lastfile);

            std::string typenamestr = std::string(qualType_string.value());

            if (auto bracket = typenamestr.find_first_of('[');
                bracket != std::string::npos) {
                typenamestr.insert(bracket,
                                   std::format(" {}", name_string.value()));
            } else {
                typenamestr += std::format(" {}", name_string.value());
            }

            context_->addDeclaration(std::format("extern {};", typenamestr));

            VarDecl var;

            auto type_var = type["desugaredQualType"];

            var.name = name_string.value();
            var.type = type_var.error() ? "" : type_var.get_string().value();
            var.qualType = qualType_string.value();
            var.kind = kind_string.value();
            var.file = lastfile;
            var.line = lastLine;

            vars.push_back(std::move(var));
        }
    }
}

int ContextualAstAnalyzer::analyzeASTFromJsonString(
    std::string_view json, const std::string &source,
    std::vector<VarDecl> &vars) {

    // Debug output
    {
        std::fstream debugOutput("debug_output.json",
                                 std::ios::out | std::ios::trunc);
        debugOutput << json << std::endl;
    }

    simdjson::ondemand::parser parser;
    simdjson::ondemand::document doc;
    simdjson::padded_string json_buf(json);
    auto error = parser.iterate(json_buf).get(doc);
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

    auto inner = doc["inner"];
    analyzeInnerAST(source, vars, &inner);

    // Salva o header se houve mudanças
    if (context_->hasHeaderChanged()) {
        context_->saveHeaderToFile("decl_amalgama.hpp");
    }

    return EXIT_SUCCESS;
}

int ContextualAstAnalyzer::analyzeASTFile(const std::string &filename,
                                          const std::string &source,
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

} // namespace analysis
