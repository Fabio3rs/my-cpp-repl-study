#include "include/analysis/ast_context.hpp"

#include "repl.hpp"
#include "simdjson.h"
#include <cassert>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
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

bool AstContext::staticSaveHeaderToFile(const std::string &filename) {
    std::scoped_lock<std::mutex> lock(contextWriteMutex);
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
            (kind_string.value() == "CXXRecordDecl" ||
             kind_string.value() == "RecordDecl") &&
            element["inner"].error() == simdjson::SUCCESS) {

            // Extrair e adicionar a definição completa da classe/struct
            extractCompleteClassDefinition(element, source, lastfile, lastLine);

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

    return EXIT_SUCCESS;
}

int ContextualAstAnalyzer::analyzeASTFile(const std::string &filename,
                                          const std::string &source,
                                          std::vector<VarDecl> &vars) {

    simdjson::padded_string json;
    if (::verbosityLevel >= 2) {
        std::cout << std::format("loading: {}\n", filename);
    }
    auto error = simdjson::padded_string::load(filename).get(json);
    if (error) {
        std::cout << std::format("could not load the file {}\n", filename);
        std::cout << "error code: " << error << std::endl;
        return EXIT_FAILURE;
    } else if (::verbosityLevel >= 2) {
        std::cout << std::format("loaded: {} bytes.\n", json.size());
    }
    return analyzeASTFromJsonString(json, source, vars);
}

void ContextualAstAnalyzer::extractCompleteClassDefinition(
    simdjson::simdjson_result<simdjson::ondemand::value> &element,
    const std::filesystem::path &source, const std::filesystem::path &lastfile,
    int64_t lastLine) {

    using simdjson::ondemand::object;

    if (::verbosityLevel >= 4) {
        std::cout << "🔍 extractCompleteClassDefinition called" << std::endl;
    }

    try {
        // Sempre trabalhe em cima de um objeto explicitamente
        auto obj_res = element.get_object();
        if (obj_res.error()) {
            if (::verbosityLevel >= 1) {
                std::cerr << "⚠️  JSON não é um objeto: " << obj_res.error()
                          << std::endl;
            }
            return;
        }
        object obj = obj_res.value();

        // (Opcional) dump para debug sem consumir: to_json_string + reset
        /*if (::verbosityLevel >= 4) {
            auto dump = simdjson::to_json_string(obj).value();
            std::cout << dump << std::endl;
            obj.reset(); // IMPORTANTE: reposiciona o cursor para continuar
                         // acessando
        }*/

        // Campos básicos (ordem indiferente graças ao find_field_unordered)
        auto kind = obj.find_field_unordered("kind").get_string().value();
        auto name = obj.find_field_unordered("name").get_string().value();

        if (::verbosityLevel >= 4) {
            std::cout << std::format("🔍 Processing {} named '{}'\n", kind,
                                     name);
        }

        // Ignorar de outros arquivos
        std::error_code ec;
        if (!source.empty() && !lastfile.empty() &&
            !std::filesystem::equivalent(lastfile, source, ec) && !ec) {
            if (::verbosityLevel >= 4) {
                std::cout << std::format("🔍 Skipping {} from different file\n",
                                         name);
            }
            return;
        }

        // Ignorar implícitos
        auto isImplicit_val = obj.find_field_unordered("isImplicit");
        if (!isImplicit_val.error() && isImplicit_val.get_bool().value()) {
            if (::verbosityLevel >= 4)
                std::cout << std::format("🔍 Skipping implicit {}\n", name);
            return;
        }

        // Verificar definição completa
        auto completeDefinition =
            obj.find_field_unordered("completeDefinition");
        if (completeDefinition.error() ||
            !completeDefinition.get_bool().value()) {
            if (::verbosityLevel >= 4) {
                std::cout << std::format(
                    "🔍 Skipping incomplete definition of {}\n", name);
            }
            return;
        }

        if (::verbosityLevel >= 4) {
            std::cout << std::format(
                "🔍 Debug: Attempting to access range for {}\n", name);
        }
        if (::verbosityLevel >= 3) {
            std::cout << std::format(
                "📝 extractCompleteClassDefinition reached for: {}\n", name);
        }
        if (::verbosityLevel >= 2) {
            std::cout << std::format(
                "⚠️  Source extraction not implemented yet for: {}\n", name);
        }

        // ---- Extração dos offsets via find_field_unordered ----
        auto range_obj = obj.find_field_unordered("range").get_object();
        if (range_obj.error()) {
            if (::verbosityLevel >= 1) {
                std::cerr << "⚠️  No range info for: " << name << " "
                          << range_obj.error() << std::endl;
            }
            return;
        }
        auto range = range_obj.value();

        /*if (::verbosityLevel >= 4) {
            auto dump = simdjson::to_json_string(range).value();
            std::cout << "Range found for " << name << ": " << dump << "\n";
            range.reset(); // <<< fundamental se você for acessar 'begin' e
                           // 'end' depois
        }*/

        auto begin = range.find_field_unordered("begin").get_object().value();

        auto begin_offset_res =
            begin.find_field_unordered("offset").get_int64();
        if (begin_offset_res.error()) {
            if (::verbosityLevel >= 1) {
                auto dump = simdjson::to_json_string(begin).value();
                std::cerr << "⚠️  No begin offset for: " << name << " "
                          << begin_offset_res.error() << "  " << dump
                          << std::endl;
                begin.reset();
            }
            return;
        }
        auto end = range.find_field_unordered("end").get_object().value();
        auto end_offset_res = end.find_field_unordered("offset").get_int64();
        auto tokLeng = end.find_field_unordered("tokLen").get_int64();
        if (end_offset_res.error()) {
            if (::verbosityLevel >= 1) {
                std::cerr << "⚠️  No end offset for: " << name << " "
                          << end_offset_res.error() << "  " << end << std::endl;
            }
            return;
        }

        if (tokLeng.error()) {
            if (::verbosityLevel >= 1) {
                std::cerr << "⚠️  No tokLen for: " << name << " "
                          << tokLeng.error() << "  " << end << std::endl;
            }
            return;
        }

        const int64_t begin_value = begin_offset_res.value();
        const int64_t end_value = end_offset_res.value();
        const int64_t tok_length = tokLeng.value();

        if (begin_value >= end_value) {
            if (::verbosityLevel >= 1) {
                std::cerr << "⚠️  Invalid range offsets for: " << name
                          << std::endl;
            }
            return;
        }

        // ---- Leitura ipsis litteris do trecho do arquivo-fonte ----
        std::fstream sourceFileStream(lastfile,
                                      std::ios::in | std::ios::binary);
        if (!sourceFileStream.is_open()) {
            if (::verbosityLevel >= 1) {
                std::cerr << "⚠️  Could not open source file: " << lastfile
                          << std::endl;
            }
            return;
        }

        sourceFileStream.seekg(0, std::ios::end);
        const auto fileSize = static_cast<int64_t>(sourceFileStream.tellg());
        sourceFileStream.seekg(0, std::ios::beg);

        if (begin_value < 0 || end_value < 0 || begin_value >= fileSize ||
            end_value > fileSize) {
            if (::verbosityLevel >= 1) {
                std::cerr << "⚠️  Range offsets out of bounds for file: "
                          << lastfile << std::endl;
            }
            return;
        }

        const size_t length =
            static_cast<size_t>((end_value - begin_value) + tok_length);
        std::string sourceDefinition;
        sourceDefinition.reserve(length + 1);

        sourceFileStream.seekg(begin_value, std::ios::beg);
        std::copy_n(std::istreambuf_iterator<char>(sourceFileStream), length,
                    std::back_insert_iterator<std::string>(sourceDefinition));
        sourceFileStream.close();

        sourceDefinition.push_back(';');

        if (!sourceDefinition.empty()) {
            if (::verbosityLevel >= 3) {
                std::cout << std::format(
                    "Copying source definition ipsis litteris: {}\n", name);
            }
            context_->addLineDirective(lastLine, lastfile);
            context_->addDeclaration(sourceDefinition);
        } else {
            if (::verbosityLevel >= 2) {
                std::cout << std::format(
                    "⚠️  Failed to extract source definition for: {}\n", name);
            }
        }

    } catch (const std::exception &e) {
        if (::verbosityLevel >= 1) {
            std::cerr << "⚠️  Error extracting class definition: " << e.what()
                      << std::endl;
        }
    } catch (...) {
        if (::verbosityLevel >= 1) {
            std::cerr << "⚠️  Unknown error extracting class definition"
                      << std::endl;
        }
    }
}

} // namespace analysis
