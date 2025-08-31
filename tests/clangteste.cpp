// Compilar (exemplo): clang++ -std=gnu++20 cc.cpp -lclang-cpp
#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Basic/LangOptions.h>
#include <clang/Basic/TargetInfo.h>
#include <clang/Basic/TargetOptions.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/CompilerInvocation.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Lex/PreprocessorOptions.h>
#include <clang/Sema/CodeCompleteConsumer.h>
#include <llvm/ADT/IntrusiveRefCntPtr.h>
// #include <llvm/Support/Host.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/VirtualFileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// Helper simples para rodar comando e capturar stdout+stderr
static std::string runAndCapture(const std::string &cmd) {
    std::array<char, 4096> buf{};
    std::string out;
#ifdef _WIN32
    FILE *pipe = _popen((cmd + " 2>&1").c_str(), "r");
#else
    FILE *pipe = popen((cmd + " 2>&1").c_str(), "r");
#endif
    if (!pipe)
        return out;
    while (fgets(buf.data(), (int)buf.size(), pipe))
        out.append(buf.data());
#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    return out;
}

static std::string detectResourceDir() {
    auto out = runAndCapture("clang++ -print-resource-dir");
    // strip CR/LF
    out.erase(std::remove(out.begin(), out.end(), '\r'), out.end());
    out.erase(std::remove(out.begin(), out.end(), '\n'), out.end());
    return out;
}

static std::vector<std::string> detectSystemIncludeDirs() {
    std::string out = runAndCapture("clang++ -E -x c++ - -v < /dev/null");
    std::vector<std::string> dirs;
    std::istringstream iss(out);
    std::string line;
    bool inBlock = false;
    while (std::getline(iss, line)) {
        if (line.find("#include <...> search starts here:") !=
                std::string::npos ||
            line.find("#include \"...\" search starts here:") !=
                std::string::npos) {
            inBlock = true;
            continue;
        }
        if (inBlock) {
            if (line.find("End of search list.") != std::string::npos)
                break;
            // trim
            std::string s = line;
            while (!s.empty() && std::isspace((unsigned char)s.front()))
                s.erase(s.begin());
            while (!s.empty() && std::isspace((unsigned char)s.back()))
                s.pop_back();
            if (!s.empty() && s[0] == '(')
                continue;
            if (!s.empty())
                dirs.push_back(s);
        }
    }
    return dirs;
}

// Consumer que coleta e imprime as alternativas de completion
struct MyCCConsumer : public clang::CodeCompleteConsumer {
    using Result = std::pair<unsigned, std::string>;
    std::vector<Result> Items;

    explicit MyCCConsumer(const clang::CodeCompleteOptions &Opts)
        : clang::CodeCompleteConsumer(Opts) {}

    void ProcessCodeCompleteResults(clang::Sema &S,
                                    clang::CodeCompletionContext Context,
                                    clang::CodeCompletionResult *Results,
                                    unsigned NumResults) override {
        for (unsigned i = 0; i < NumResults; ++i) {
            clang::CodeCompletionString *CCS =
                Results[i].CreateCodeCompletionString(
                    S, getAllocator(), getCodeCompletionTUInfo(),
                    /*IncludeBriefComments*/ true, {});
            // Procurar o chunk "TypedText" (texto que o usuário digita)
            std::string typed;
            for (unsigned j = 0; j < CCS->getNumChunks(); ++j) {
                auto K = CCS->getChunkKind(j);
                if (K == clang::CodeCompletionString::CK_TypedText) {
                    typed = CCS->getChunkText(j).str();
                    break;
                }
            }
            if (!typed.empty()) {
                unsigned prio = Results[i].Priority; // menor = mais relevante
                Items.emplace_back(prio, typed);
            }
        }
    }

    void ProcessOverloadCandidates(clang::Sema &, unsigned, OverloadCandidate *,
                                   unsigned) {
        // ignorado neste exemplo
    }
};

int main() {
    // Código “unsaved” em memória
    const char *filename = "buffer.cpp";
    const char *contents = "#include <iostream>\n"
                           "int main(){\n"
                           "  std::cou\n"
                           "}\n";

    // === 1) Monta o CompilerInvocation a partir de “args” ===
    std::vector<const char *> args = {"-x", "c++", "-std=gnu++20"};
    std::string resourceDir = detectResourceDir();
    if (!resourceDir.empty()) {
        args.push_back("-resource-dir");
        args.push_back(resourceDir.c_str());
    }
    auto sysInc = detectSystemIncludeDirs();
    std::vector<std::string> isystemArgs; // para manter storage estável
    for (auto &d : sysInc) {
        isystemArgs.push_back("-isystem");
        isystemArgs.push_back(d);
    }
    std::vector<const char *> argsAll = args;
    for (auto &s : isystemArgs)
        argsAll.push_back(s.c_str());

    llvm::IntrusiveRefCntPtr<clang::DiagnosticOptions> diagOpts =
        new clang::DiagnosticOptions();
    llvm::IntrusiveRefCntPtr<clang::DiagnosticsEngine> diags =
        clang::CompilerInstance::createDiagnostics(
            diagOpts.get(), /*DiagConsumer*/ nullptr, false);

    auto invocation = std::make_shared<clang::CompilerInvocation>();
    clang::CompilerInvocation::CreateFromArgs(*invocation, argsAll, *diags);

    // === 2) CompilerInstance básico ===
    clang::CompilerInstance CI;
    CI.setInvocation(invocation);

    // Target
    auto TO = std::make_shared<clang::TargetOptions>();
    TO->Triple = llvm::sys::getDefaultTargetTriple();
    CI.setTarget(clang::TargetInfo::CreateTargetInfo(*diags, TO));

    // VFS com arquivo em memória
    auto InMemFS = llvm::IntrusiveRefCntPtr<llvm::vfs::InMemoryFileSystem>(
        new llvm::vfs::InMemoryFileSystem);
    InMemFS->addFile(filename, 0,
                     llvm::MemoryBuffer::getMemBuffer(contents, filename));
    auto Overlay = llvm::IntrusiveRefCntPtr<llvm::vfs::OverlayFileSystem>(
        new llvm::vfs::OverlayFileSystem(llvm::vfs::getRealFileSystem()));
    Overlay->pushOverlay(InMemFS);

    CI.createFileManager(Overlay);
    CI.createSourceManager(CI.getFileManager());

    // === 3) Configura entrada e posição de code-completion ===
    CI.getFrontendOpts().Inputs.clear();
    CI.getFrontendOpts().Inputs.emplace_back(filename, clang::Language::CXX);
    CI.getFrontendOpts().ProgramAction =
        clang::frontend::ParseSyntaxOnly; // não emite código
    CI.getFrontendOpts().CodeCompletionAt = {
        filename, 3, 11}; // linha 3, col 11 (após "std::cou")
    // (ver FrontendOptions::CodeCompletionAt)  // docs citadas abaixo

    // === 4) Preprocessador/AST/Sema e Consumer de completion ===
    CI.createPreprocessor(clang::TU_Complete);
    CI.createASTContext();

    clang::CodeCompleteOptions CCOpts; // habilite o que quiser aqui
    auto *Consumer = new MyCCConsumer(CCOpts);
    CI.setCodeCompletionConsumer(Consumer); // o CI passa a ser dono

    CI.createSema(clang::TU_Complete, /*CompletionConsumer*/ Consumer);

    // === 5) Executa a ação: o parser dispara completion no ponto pedido ===
    clang::SyntaxOnlyAction action; // só parseia (aciona completion)
    bool ok = CI.ExecuteAction(action);

    if (!ok) {
        llvm::errs() << "Falha ao parsear/complete\n";
        return 1;
    }

    // === 6) Mostra resultados (ordenando por prioridade crescente) ===
    auto *CC = static_cast<MyCCConsumer *>(Consumer); // já é dono do CI
    std::stable_sort(CC->Items.begin(), CC->Items.end(),
                     [](auto &a, auto &b) { return a.first < b.first; });

    llvm::outs() << "Sugestões:\n";
    unsigned count = 0;
    for (auto &it : CC->Items) {
        if (count++ >= 100)
            break;
        llvm::outs() << "  " << it.second << "\n";
    }

    return 0;
}
