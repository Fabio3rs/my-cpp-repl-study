// mini_clang_service.cpp
#include <algorithm>
#include <array>
#include <cstdio>
#include <iostream>
#include <memory>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include <llvm/ADT/IntrusiveRefCntPtr.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/VirtualFileSystem.h>
#include <llvm/Support/raw_ostream.h>

#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/CompilerInvocation.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Frontend/PrecompiledPreamble.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/Lex/PreprocessorOptions.h>
#include <clang/Sema/CodeCompleteConsumer.h> // PrintingCodeCompleteConsumer

// -------------------- util: executar comando e capturar stdout
// --------------------
static std::string runAndCapture(const std::string &cmd) {
    std::array<char, 4096> buf{};
    std::string out;
    FILE *pipe = popen((cmd + " 2>&1").c_str(), "r");
    if (!pipe)
        return out;
    while (fgets(buf.data(), buf.size(), pipe))
        out.append(buf.data());
    pclose(pipe);
    return out;
}

// -------------------- sondagem do clang++: resource-dir e include dirs
// --------------------
static std::string detectResourceDir() {
    std::string out = runAndCapture("clang++ -print-resource-dir");
    // trim CR/LF
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
            // linhas com caminho costumam vir com indent
            std::string s = line;
            while (!s.empty() && isspace((unsigned char)s.front()))
                s.erase(s.begin());
            while (!s.empty() && isspace((unsigned char)s.back()))
                s.pop_back();
            if (!s.empty() && s[0] != '(')
                dirs.push_back(s);
        }
    }
    return dirs;
}

// -------------------- serviço --------------------
class MiniClangService {
  public:
    std::string RD;
    std::vector<std::string> systemIncludes;

    MiniClangService() {
        // 1) Descobrir args "como o clang++"
        baseArgs_ = {"-x", "c++", "-std=gnu++20", "-fsyntax-only"};

        // resource-dir
        if (RD = detectResourceDir(); !RD.empty()) {
            baseArgs_.push_back("-resource-dir");
            baseArgs_.push_back(RD.c_str());
        }

        systemIncludes = detectSystemIncludeDirs();
        // -isystem (ordem igual ao clang++)
        for (const auto &inc : systemIncludes) {
            baseArgs_.push_back("-isystem");
            baseArgs_.push_back(inc.c_str());
        }

        baseArgs_.push_back("main.cc");

        // 2) Construir Diagnostics para a base (reutilizável)
        diagOpts_ = llvm::IntrusiveRefCntPtr<clang::DiagnosticOptions>(
            new clang::DiagnosticOptions());
        diagPrinter_ = std::make_unique<clang::TextDiagnosticPrinter>(
            llvm::errs(), &*diagOpts_);
        diags_ = std::make_unique<clang::DiagnosticsEngine>(
            new clang::DiagnosticIDs, &*diagOpts_, diagPrinter_.get(),
            /*ShouldOwnClient=*/false);

        // 3) Criar invocação base a partir dos args sonda/driver
        baseInvocation_ = std::make_shared<clang::CompilerInvocation>();
        clang::CompilerInvocation::CreateFromArgs(
            *baseInvocation_, llvm::ArrayRef<const char *>(baseArgs_), *diags_);

        // Modo "serviço": a invocação base fica guardada; preâmbulo/AST serão
        // reusados.
        vfs_ = llvm::vfs::getRealFileSystem();
    }

    // Requisição de completion/diagnóstico para um snippet.
    // O 'snippet' vira um TU pequeno e ajustamos CodeCompletionAt.
    void codeComplete(const std::string &snippet, unsigned line,
                      unsigned column) {
        // 1) Materializa TU em memória (arquivo "main.cc")
        const char *MainName = "main.cc";
        auto MB = llvm::MemoryBuffer::getMemBufferCopy(snippet, MainName);

        // 2) Atualiza/gera preâmbulo (reuso quando possível)
        ensurePreamble(*MB);

        // 3) Monta uma CompilerInstance a partir da base (sem custo de parse de
        // args)
        auto CI = std::make_unique<clang::CompilerInstance>();
        auto DiagOpts = llvm::IntrusiveRefCntPtr<clang::DiagnosticOptions>(
            new clang::DiagnosticOptions());
        auto Diags = llvm::IntrusiveRefCntPtr<clang::DiagnosticsEngine>(
            clang::CompilerInstance::createDiagnostics(
                DiagOpts.get(), /*Client=*/nullptr, /*Own*/ false));

        auto Inv =
            std::make_shared<clang::CompilerInvocation>(*baseInvocation_);
        Inv->getFrontendOpts().ProgramAction = clang::frontend::ParseSyntaxOnly;
        Inv->getFrontendOpts().CodeCompletionAt.FileName = MainName;
        Inv->getFrontendOpts().CodeCompletionAt.Line = line;
        Inv->getFrontendOpts().CodeCompletionAt.Column = column;

        CI->setInvocation(std::move(Inv));
        CI->setDiagnostics(Diags.get());
        CI->createFileManager();
        CI->createSourceManager(CI->getFileManager());

        // Remapeia o arquivo principal para o buffer em memória (unsaved)
        CI->getPreprocessorOpts().addRemappedFile(
            MainName, const_cast<llvm::MemoryBuffer *>(MB.get()));

        // 4) Preprocessador com preâmbulo implícito
        CI->createPreprocessor(clang::TU_Complete);
        if (preamble_) {
            preamble_->AddImplicitPreamble(
                CI->getInvocation(), vfs_,
                MB.get()); // reaplica PCH na invocação
        }

        CI->createASTContext();

        // 5) Consumidor de completion pronto (imprime sugestões)
        clang::CodeCompleteOptions CCOpts;
        CCOpts.IncludeGlobals = true;
        CCOpts.IncludeMacros = true;
        CCOpts.IncludeBriefComments = true;
        auto *Consumer =
            new clang::PrintingCodeCompleteConsumer(CCOpts, llvm::outs()); //
        CI->setCodeCompletionConsumer(Consumer);

        // 6) Executa ação mínima para disparar a Sema + completion
        clang::SyntaxOnlyAction Act;
        (void)CI->ExecuteAction(Act);
        // Obs.: o PrintingCodeCompleteConsumer imprime no stdout.
        // Você pode capturar e transformar em sua própria lista de itens.
    }

  private:
    void ensurePreamble(const llvm::MemoryBuffer &MainBuf) {
        // Calcula limite do preâmbulo usando as LangOpts correntes
        auto LO = baseInvocation_->getLangOpts();
        auto B = clang::ComputePreambleBounds(
            LO, MainBuf.getMemBufferRef(),
            /*MaxLines=*/0); // :contentReference[oaicite:5]{index=5}

        if (preamble_ &&
            preamble_->CanReuse(*baseInvocation_, MainBuf.getMemBufferRef(), B,
                                *vfs_)) {
            return; // segue usando o preâmbulo atual
        }

        auto DiagOpts = llvm::IntrusiveRefCntPtr<clang::DiagnosticOptions>(
            new clang::DiagnosticOptions());
        auto Diags = clang::CompilerInstance::createDiagnostics(
            DiagOpts.get(),
            /*Client=*/nullptr, /*Own=*/false);
        auto PCHOps = std::make_shared<clang::PCHContainerOperations>();
        clang::PreambleCallbacks Cbs;

        // Constrói/atualiza o preâmbulo (em memória)
        auto Built = clang::PrecompiledPreamble::Build(
            *baseInvocation_, &MainBuf, B, *Diags, vfs_, PCHOps,
            /*StoreInMemory=*/true, /*StoragePath=*/"",
            Cbs); // :contentReference[oaicite:6]{index=6}

        if (Built)
            preamble_ =
                std::make_unique<clang::PrecompiledPreamble>(std::move(*Built));
        // caso contrário, prossiga sem preâmbulo (funciona, só fica mais lento)
    }

  private:
    // Estado “de serviço”
    std::vector<const char *> baseArgs_;
    llvm::IntrusiveRefCntPtr<clang::DiagnosticOptions> diagOpts_;
    std::unique_ptr<clang::TextDiagnosticPrinter> diagPrinter_;
    std::unique_ptr<clang::DiagnosticsEngine> diags_;
    std::shared_ptr<clang::CompilerInvocation> baseInvocation_;
    std::unique_ptr<clang::PrecompiledPreamble> preamble_;
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> vfs_;
};

// -------------------- Exemplo de uso --------------------
static std::string makeSnippet() {
    // Simula um TU mínimo com headers padrão (como clang++ faria)
    std::ostringstream oss;
    oss << "#include <iostream>\n";
    oss << "#include <vector>\n";
    oss << "#include <string>\n";
    oss << "int main(){\n";
    oss << "  std::cou\n"; // completion depois de "std::cou"
    oss << "}\n";
    return oss.str();
}

int main() {
    MiniClangService
        svc; // fica residente (pode reusar baseInvocation/preamble)
    auto snippet = makeSnippet();

    // linha 5, coluna após "  std::cou" -> 2 espaços + 9 chars = 11
    svc.codeComplete(snippet, /*line=*/5, /*column=*/11);
    return 0;
}
