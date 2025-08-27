#include "analysis/clang_ast_adapter.hpp"

#include "analysis/ast_context.hpp"
#include "repl.hpp"

namespace analysis {

ClangAstAnalyzerAdapter::ClangAstAnalyzerAdapter() {
    context_ = std::make_shared<AstContext>();
    analyzer_ = std::make_unique<ContextualAstAnalyzer>(context_);
}

ClangAstAnalyzerAdapter::ClangAstAnalyzerAdapter(
    std::shared_ptr<AstContext> context,
    std::unique_ptr<ContextualAstAnalyzer> analyzer)
    : context_(std::move(context)), analyzer_(std::move(analyzer)) {

    // Se o contexto não foi fornecido, criar um novo
    if (!context_) {
        context_ = std::make_shared<AstContext>();
    }

    // Se o analisador não foi fornecido, criar um novo com o contexto
    if (!analyzer_) {
        analyzer_ = std::make_unique<ContextualAstAnalyzer>(context_);
    }
}

int ClangAstAnalyzerAdapter::analyzeJson(std::string_view json,
                                         const std::string &source,
                                         std::vector<VarDecl> &vars) {
    if (!analyzer_) {
        return -1;
    }
    return analyzer_->analyzeASTFromJsonString(json, source, vars);
}

int ClangAstAnalyzerAdapter::analyzeFile(const std::string &jsonFilename,
                                         const std::string &source,
                                         std::vector<VarDecl> &vars) {
    if (!analyzer_) {
        return -1;
    }
    return analyzer_->analyzeASTFile(jsonFilename, source, vars);
}

std::shared_ptr<AstContext> ClangAstAnalyzerAdapter::getContext() const {
    return context_;
}

const ContextualAstAnalyzer &ClangAstAnalyzerAdapter::getAnalyzer() const {
    return *analyzer_;
}

ClangAstAnalyzerAdapter ClangAstAnalyzerAdapter::createWithSharedContext(
    std::shared_ptr<AstContext> context) {
    return ClangAstAnalyzerAdapter(context, nullptr);
}

} // namespace analysis
