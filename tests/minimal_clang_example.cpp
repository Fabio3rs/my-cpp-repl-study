// g++ minimal_ccp.cpp -o minimal_ccp  (ver CMake abaixo)
// Código: pede completion depois de "std::cou|" (linha 6, col 18)

#include <array>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

#include <clang/Basic/DiagnosticOptions.h>
#include <clang/Basic/Version.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/CompilerInvocation.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Frontend/TextDiagnosticPrinter.h>
#include <clang/Lex/PreprocessorOptions.h>
#include <clang/Sema/CodeCompleteConsumer.h> // PrintingCodeCompleteConsumer
#include <llvm/ADT/IntrusiveRefCntPtr.h>
#include <llvm/Support/VirtualFileSystem.h>

namespace {

// Executa um comando e captura stdout
static std::string runAndCapture(const std::string &cmd) {
    std::array<char, 4096> buf{};
    std::string out;
    FILE *pipe = popen((cmd + " 2>&1").c_str(), "r"); // também captura stderr
    if (!pipe)
        return out;
    while (fgets(buf.data(), buf.size(), pipe))
        out.append(buf.data());
    pclose(pipe);
    return out;
}

// Descobre o resource-dir do clang++ do sistema
static std::string detectResourceDir() {
    std::string out = runAndCapture("clang++-19 -print-resource-dir");
    // remove quebras de linha
    out.erase(std::remove(out.begin(), out.end(), '\r'), out.end());
    out.erase(std::remove(out.begin(), out.end(), '\n'), out.end());
    return out;
}

// Extrai include paths do bloco “search starts here”
static std::vector<std::string> detectSystemIncludeDirs() {
    std::string out = runAndCapture("clang++-19 -E -x c++ - -v < /dev/null");
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
            // linhas válidas começam com espaço + caminho
            std::string s = line;
            // trim
            while (!s.empty() &&
                   std::isspace(static_cast<unsigned char>(s.front())))
                s.erase(s.begin());
            while (!s.empty() &&
                   std::isspace(static_cast<unsigned char>(s.back())))
                s.pop_back();
            if (!s.empty() && s[0] == '(')
                continue; // ignora “(framework directory)”, etc.
            if (!s.empty())
                dirs.push_back(s);
        }
    }
    return dirs;
}

void initializeDiagnostics(clang::CompilerInstance &ci) {
    // Clang 20+ requires passing the VFS; older releases provide the default
    // parameterless overload. Keep both to match the CI toolchain (Clang 18/19)
    // and newer developer setups.
#if CLANG_VERSION_MAJOR >= 20
    ci.createDiagnostics(ci.getVirtualFileSystem());
#else
    ci.createDiagnostics();
#endif
}

void initializeDiagnostics(clang::CompilerInstance &ci,
                           clang::DiagnosticConsumer *client,
                           bool shouldOwnClient) {
#if CLANG_VERSION_MAJOR >= 20
    ci.createDiagnostics(ci.getVirtualFileSystem(), client, shouldOwnClient);
#else
    ci.createDiagnostics(client, shouldOwnClient);
#endif
}

int testeEmC() {
    CXIndex index = clang_createIndex(0, 0);

    const char *filename = "buffer.cpp";
    const char *contents = "#include <iostream>\n#include <memory>\n#include "
                           "<array>\n#include <atomic>\n"
                           "int main(){\n"
                           "  std::cou\n"
                           "}\n";

    CXUnsavedFile unsaved;
    unsaved.Filename = filename;
    unsaved.Contents = contents;
    unsaved.Length = strlen(contents);

    std::vector<const char *> args = {"-x", "c++", "-std=gnu++20"};

    // Adiciona resource-dir
    auto resourceDir = detectResourceDir();
    if (!resourceDir.empty()) {
        args.push_back("-resource-dir");
        args.push_back(resourceDir.c_str());
    }
    auto includes = detectSystemIncludeDirs();
    for (const auto &d : includes) {
        args.push_back("-isystem");
        args.push_back(d.c_str());
    }

    unsigned opts = clang_defaultCodeCompleteOptions() |
                    CXCodeComplete_IncludeBriefComments; // opcional
    CXTranslationUnit tu = clang_parseTranslationUnit(
        index, filename, args.data(), (int)args.size(), &unsaved, 1,
        CXTranslationUnit_None // evite _Incomplete aqui
    );

    if (!tu) {
        return 1;
    }

    CXCodeCompleteResults *results = clang_codeCompleteAt(
        tu, filename, 6, 11, // linha 6, coluna 11 (após "std::cou")
        &unsaved, 1,         // também informar unsaved aqui
        opts);

    if (results) {
        printf("Sugestões:\n");
        std::stable_sort(
            results->Results, results->Results + results->NumResults,
            [](const CXCompletionResult &a, const CXCompletionResult &b) {
                unsigned pa = clang_getCompletionPriority(a.CompletionString);
                unsigned pb = clang_getCompletionPriority(b.CompletionString);
                return pa < pb; // menor = mais relevante
            });

        for (unsigned i = 0; i < results->NumResults && i < 100; ++i) {
            CXCompletionString cs = results->Results[i].CompletionString;
            for (unsigned j = 0; j < clang_getNumCompletionChunks(cs); ++j) {
                if (clang_getCompletionChunkKind(cs, j) ==
                    CXCompletionChunk_TypedText) {
                    CXString text = clang_getCompletionChunkText(cs, j);
                    printf("  %s\n", clang_getCString(text));
                    clang_disposeString(text);
                }
            }
        }
        clang_disposeCodeCompleteResults(results);
    }

    clang_disposeTranslationUnit(tu);
    clang_disposeIndex(index);

    return 0;
}

int testeEmCpp() {
    // 1) Preparar CompilerInstance + Diagnostics (igual ao seu exemplo)
    clang::CompilerInstance CI;
    auto DiagOpts = std::make_shared<clang::DiagnosticOptions>();
    initializeDiagnostics(CI);

    // 2) Invocation (args mínimos)
    auto Inv = std::make_shared<clang::CompilerInvocation>();
    const char *fname = "buffer.cpp";
    const char *code = "#include <iostream>\n#include <memory>\n#include "
                       "<array>\n#include <atomic>\nint main(){ std::cou }\n";
    std::vector<const char *> args = {"-x", "c++", "-std=gnu++20", fname};

    auto resourceDir = detectResourceDir();
    if (!resourceDir.empty()) {
        args.push_back("-resource-dir");
        args.push_back(resourceDir.c_str());
    }
    auto includes = detectSystemIncludeDirs();
    for (const auto &d : includes) {
        args.push_back("-isystem");
        args.push_back(d.c_str());
    }

    clang::CompilerInvocation::CreateFromArgs(*Inv, args, CI.getDiagnostics());
    CI.setInvocation(Inv);

    // 3) Remap: “buffer.cpp” -> conteúdo em memória (unsaved)
    auto MB = llvm::MemoryBuffer::getMemBufferCopy(code, fname);
    // Caso queira que o CI cuide da vida útil, deixe o flag padrão (false) e
    // passe o ponteiro
    CI.getPreprocessorOpts().addRemappedFile(
        fname, MB.release()); // <-- chave do unsaved
    // (Se preferir manter você mesmo o buffer, veja RetainRemappedFileBuffers.)
    // :contentReference[oaicite:1]{index=1}

    // 4) Ponto de completion
    CI.getFrontendOpts().CodeCompletionAt.FileName = fname;
    CI.getFrontendOpts().CodeCompletionAt.Line = 5; // linha do "std::cou"
    CI.getFrontendOpts().CodeCompletionAt.Column = 20;

    // 5) Consumer pronto de completion (API C++)
    clang::CodeCompleteOptions CC;
    auto *Consumer = new clang::PrintingCodeCompleteConsumer(CC, llvm::outs());
    CI.setCodeCompletionConsumer(Consumer);

    // 6) Rodar ação que aciona a Sema e gera completion
    clang::SyntaxOnlyAction Act;
    if (!CI.ExecuteAction(Act))
        return 1;
    return 0;
}

} // namespace

int main() {
    testeEmC();

    std::cout << "Teste em C++\n";
    testeEmCpp();

    std::cout << "Outro teste em C++ com detalhes\n";

    // 1) Prepara um arquivo de teste
    const char *fname = "sample2.cpp";
    const char *code = "#include <iostream>\n"
                       "int main(){\n"
                       "  std::cou\n"
                       "}\n";
    // std::ofstream(fname) << code;

    // 2) Diagnósticos
    auto diagOpts = llvm::IntrusiveRefCntPtr<clang::DiagnosticOptions>(
        new clang::DiagnosticOptions());
    auto diagPrinter =
        new clang::TextDiagnosticPrinter(llvm::errs(), &*diagOpts);
    llvm::IntrusiveRefCntPtr<clang::DiagnosticIDs> diagIDs(
        new clang::DiagnosticIDs());
    clang::DiagnosticsEngine diags(diagIDs, &*diagOpts, diagPrinter);

    // 3) CompilerInvocation a partir de "args"
    std::vector<const char *> args = {
        "clang-tool", "-x", "c++", "-std=gnu++20", "-fsyntax-only", fname};

    // Adiciona resource-dir
    auto resourceDir = detectResourceDir();
    if (!resourceDir.empty()) {
        args.push_back("-resource-dir");
        args.push_back(resourceDir.c_str());
    }
    auto includes = detectSystemIncludeDirs();
    for (const auto &d : includes) {
        args.push_back("-isystem");
        args.push_back(d.c_str());
    }

    auto CI = std::make_unique<clang::CompilerInstance>();
    std::shared_ptr<clang::CompilerInvocation> Inv =
        std::make_shared<clang::CompilerInvocation>();
    clang::CompilerInvocation::CreateFromArgs(
        *Inv, llvm::ArrayRef(args).slice(1), diags); // skip argv[0]
    CI->setInvocation(Inv);
    initializeDiagnostics(*CI, diagPrinter, false);

    auto MB = llvm::MemoryBuffer::getMemBufferCopy(code, fname);
    // Caso queira que o CI cuide da vida útil, deixe o flag padrão (false) e
    // passe o ponteiro
    CI->getPreprocessorOpts().addRemappedFile(
        fname, MB.release()); // <-- chave do unsaved
    // 4) Configura o ponto de completion (linha 3+1, coluna pos 'std::cou')
    // Arquivo gerado tem:
    // 1:#include...
    // 2:int main(){
    // 3:  std::cou
    // 4:}
    CI->getFrontendOpts().CodeCompletionAt.FileName = fname;
    CI->getFrontendOpts().CodeCompletionAt.Line = 3;
    CI->getFrontendOpts().CodeCompletionAt.Column =
        11; // após 'std::cou' (contando espaços)

    // 5) Consumer de completion pronto (API C++)
    clang::CodeCompleteOptions CCOpts;
    CCOpts.IncludeMacros = true;
    CCOpts.IncludeGlobals = true;
    CCOpts.IncludeBriefComments = true;

    // PrintingCodeCompleteConsumer imprime resultados de forma simples
    auto *Consumer =
        new clang::PrintingCodeCompleteConsumer(CCOpts, llvm::outs());
    CI->setCodeCompletionConsumer(
        Consumer); // CI toma ownership. :contentReference[oaicite:1]{index=1}

    // 6) Executa uma ação de frontend mínima (só sintaxe) que aciona a Sema
    clang::SyntaxOnlyAction Action;
    const bool ok =
        CI->ExecuteAction(Action); // dispara o completion no ponto configurado
    if (!ok) {
        llvm::errs() << "Falha ao executar ação de frontend.\n";
        return 1;
    }
    code = "#include <iostream>\n"
           "int main(){\n"
           "  int teste = 10\n"
           "  std::cout << \n"
           "}\n";
    Consumer = new clang::PrintingCodeCompleteConsumer(CCOpts, llvm::outs());
    CI->setCodeCompletionConsumer(Consumer);
    MB = llvm::MemoryBuffer::getMemBufferCopy(code, fname);
    // Caso queira que o CI cuide da vida útil, deixe o flag padrão (false) e
    // passe o ponteiro
    CI->getPreprocessorOpts().addRemappedFile(
        fname, MB.release()); // <-- chave do unsaved

    CI->getFrontendOpts().CodeCompletionAt.Line = 4;
    CI->getFrontendOpts().CodeCompletionAt.Column = 16;

    {
        const bool ok = CI->ExecuteAction(
            Action); // dispara o completion no ponto configurado
        if (!ok) {
            llvm::errs() << "Falha ao executar ação de frontend.\n";
            return 1;
        }
    }
    return 0;
}
