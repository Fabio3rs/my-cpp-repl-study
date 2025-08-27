#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

// forward declarations to avoid heavy includes
struct VarDecl;

namespace analysis {
class AstContext;
}

namespace analysis {

class IAstAnalyzer {
  public:
    virtual ~IAstAnalyzer() = default;

    // Analyze a JSON AST string (clang -Xclang -ast-dump=json output in memory)
    virtual int analyzeJson(std::string_view json, const std::string &source,
                            std::vector<VarDecl> &vars) = 0;

    // Analyze a JSON AST file on disk
    virtual int analyzeFile(const std::string &jsonFilename,
                            const std::string &source,
                            std::vector<VarDecl> &vars) = 0;

    // Get the AST context (may return null if not supported)
    virtual std::shared_ptr<AstContext> getContext() const { return nullptr; }
};

} // namespace analysis
