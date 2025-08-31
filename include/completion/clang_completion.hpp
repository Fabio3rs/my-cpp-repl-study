#pragma once

#ifdef CLANG_COMPLETION_ENABLED
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Frontend/PrecompiledPreamble.h>
#include <clang/Frontend/Utils.h>
#include <clang/Lex/PreprocessorOptions.h>
#include <clang/Sema/CodeCompleteConsumer.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/CommandLine.h>
namespace completion {
class CompletionCollector;
class CCAction;
} // namespace completion
#endif

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct VarDecl;       // fwd
struct BuildSettings; // fwd

namespace completion {

struct CompletionItem {
    std::string text;
    std::string display;
    std::string documentation;
    std::string returnType;
    int priority{};
    enum class Kind {
        Variable,
        Function,
        Class,
        Struct,
        Enum,
        Keyword,
        Include,
        Macro
    } kind{Kind::Variable};
};

struct ReplContext {
    std::string currentIncludes;
    std::string variableDeclarations;
    std::string functionDeclarations;
    std::string typeDefinitions;
    std::string activeCode;
    int line = 1;
    int column = 1;
};

class ClangCompletion {
  private:
    ReplContext replContext_;
    std::unordered_map<std::string, std::vector<CompletionItem>>
        completionCache_;
    int verbosityLevel_;

    void fillDefaultArgsIfEmpty_();

#ifdef CLANG_COMPLETION_ENABLED
    std::vector<std::string> compiler_args_;
    mutable std::unordered_set<std::string> knownSymbols_;
    mutable bool symbolCacheValid_{};

    // --- Estado "quente" do TU ---
    std::shared_ptr<clang::CompilerInvocation> baseInvocation_;
    std::unique_ptr<clang::PrecompiledPreamble> preamble_;
    std::string lastMainBuffer_;       // conteúdo do main file
    unsigned lastPreambleVersion_ = 0; // incrementa quando rebuild do preamble

    // Helpers internos para TU/preamble
    void ensureInvocation_();
    bool ensurePreamble_(llvm::StringRef code);
    std::unique_ptr<clang::CompilerInstance>
    makeCompilerForParse_(llvm::StringRef code, unsigned &outLine,
                          unsigned &outCol) const;
#endif

    // Helpers independentes de libclang
    void initializeClang(const BuildSettings &settings);
    void cleanupClang();
    std::string buildTempFile(const std::string &partialCode) const;

#ifdef CLANG_COMPLETION_ENABLED
    // Helpers para preamble/unsaved files approach
    std::string buildPreamble() const;
    std::string buildCurrentCode(const std::string &partialCode) const;
    std::vector<CompletionItem>
    getCompletionsWithCompilerInstance(const std::string &fullCode,
                                       const std::string &currentCode, int line,
                                       int column);

    std::vector<CompletionItem>
    getCompletionsWithClang(const std::string &partialCode, int line,
                            int column);
    std::vector<std::string> getDiagnosticsWithClang(const std::string &code);
    void updateSymbolCache() const;
    void updateSymbolCacheFallback() const;
#endif

    std::vector<CompletionItem>
    getCompletionsMock(const std::string &partialCode, int line, int column);
    std::vector<std::string> getDiagnosticsMock(const std::string &code);

  public:
    ClangCompletion();
    explicit ClangCompletion(int verbosity) { setVerbosity(verbosity); }
    ~ClangCompletion();

    ClangCompletion(const ClangCompletion &) = delete;
    ClangCompletion &operator=(const ClangCompletion &) = delete;

    void initialize(const BuildSettings &settings);
    void setVerbosity(int verbosity) { verbosityLevel_ = verbosity; }
    int getVerbosity() const { return verbosityLevel_; }

    void updateReplContext(const ReplContext &context);

    std::vector<CompletionItem> getCompletions(const std::string &partialCode,
                                               int line, int column);
    std::string getDocumentation(const std::string &symbol);
    std::vector<std::string> getDiagnostics(const std::string &code);
    bool symbolExists(const std::string &symbol) const;
    void clearCache();
};

} // namespace completion
