#include "stdafx.hpp"

#include "repl.hpp"
#include "utility/assembly_info.hpp"
#include <algorithm>
#include <cstdint>
#include <exceptdefs.h>
#include <execinfo.h>
#include <format>
#include <getopt.h>
#include <iostream>
#include <segvcatch.h>
#include <stdexcept>
#include <string>
#include <ucontext.h>
#include <unistd.h>

#include "utility/file_raii.hpp"

using namespace std;

// Funções auxiliares para processamento multilinha
int countChar(const std::string &str, char c) {
    return std::count(str.begin(), str.end(), c);
}

bool isMultilineStart(const std::string &line) {
    // Remove espaços e verifica patterns que tipicamente iniciam blocos
    // multilinhas
    std::string trimmed = line;
    trimmed.erase(0, trimmed.find_first_not_of(" \t"));

    // Patterns que indicam início de definição multilinha
    return trimmed.starts_with("class ") || trimmed.starts_with("struct ") ||
           trimmed.starts_with("enum ") || trimmed.starts_with("namespace ") ||
           trimmed.starts_with("template") ||
           (trimmed.find('{') != std::string::npos &&
            trimmed.find('}') == std::string::npos) ||
           (trimmed.find('(') != std::string::npos &&
            countChar(trimmed, '(') > countChar(trimmed, ')'));
}

bool isCompleteStatement(const std::string &line) {
    // Verifica se é uma declaração completa em uma linha
    return line.ends_with(";") || line.ends_with("}") ||
           (countChar(line, '{') > 0 &&
            countChar(line, '{') == countChar(line, '}')) ||
           (countChar(line, '(') > 0 &&
            countChar(line, '(') == countChar(line, ')'));
}

void showVersion() {
    std::cout << std::format(
        "C++ REPL v1.0.0 - Interactive C++ Development Environment\n");
    std::cout << std::format("Built with: C++{} | Clang | Platform: Linux\n",
                             __cplusplus / 100);
    std::cout << std::format(
        "Architecture: Modular Design | Test Coverage: 95%+\n");
    std::cout << std::format(
        "Cache System: Intelligent Compilation Result Caching\n\n");
}

void showUsage(const char *programName) {
    std::cout << std::format("Usage: {} [OPTIONS]\n\n", programName);

    std::cout << "OPTIONS:\n";
    std::cout << "  -h, --help              Show this help message and exit\n";
    std::cout
        << "      --version           Show version information and exit\n";
    std::cout << "  -s, --safe              Enable signal handlers for crash "
                 "protection\n";
    std::cout << "  -r, --run FILE          Execute REPL commands from file\n";
    std::cout << "  -v, --verbose           Increase verbosity level (can be "
                 "repeated: -vvv)\n";
    std::cout << "  -q, --quiet             Suppress all non-error output\n\n";

    std::cout << "VERBOSITY LEVELS:\n";
    std::cout << "  0 (default)             Errors only\n";
    std::cout << "  1 (-v)                  + Basic operations (compilation "
                 "status)\n";
    std::cout << "  2 (-vv)                 + Detailed timing and cache info\n";
    std::cout << "  3 (-vvv)                + Command execution details\n";
    std::cout
        << "  4+ (-vvvv+)             + Debug information and AST details\n\n";

    std::cout << "EXAMPLES:\n";
    std::cout << "  " << programName
              << "                    Start interactive REPL (quiet mode)\n";
    std::cout << "  " << programName
              << " -v               Start with basic verbosity\n";
    std::cout << "  " << programName
              << " -vvv             Start with high verbosity\n";
    std::cout << "  " << programName
              << " -s -v            Safe mode with basic verbosity\n";
    std::cout << "  " << programName
              << " -q -r script.cpp Execute script in quiet mode\n\n";

    std::cout << "INTERACTIVE COMMANDS:\n";
    std::cout << "  #help                   List all available REPL commands\n";
    std::cout << "  #includedir <path>      Add include directory\n";
    std::cout << "  #lib <name>             Link with library\n";
    std::cout << "  #eval <file>            Evaluate C++ file\n";
    std::cout << "  #return <expr>          Evaluate and print expression\n";
    std::cout << "  printall                Show all variables\n";
    std::cout << "  exit                    Exit the REPL\n\n";

    std::cout << "For more information about REPL commands, start the program "
                 "and type '#help'\n";
}

void handle_segv(const segvcatch::hardware_exception_info &info) {
    throw segvcatch::segmentation_fault(
        std::format("SEGV at: {}", reinterpret_cast<uintptr_t>(info.addr)),
        info);
}

void handle_fpe(const segvcatch::hardware_exception_info &info) {
    throw segvcatch::floating_point_error(
        std::format("FPE at: {}", reinterpret_cast<uintptr_t>(info.addr)),
        info);
}

void segfaultHandler(int sig) {
    std::cerr << "Segmentation fault AAAAAAAAAAAAAA" << std::endl;

    // Get the address at the time of the fault
    ucontext_t context;
    getcontext(&context);

    std::string message = "Segmentation fault at address: ";

    void *addr = (void *)context.uc_mcontext.gregs[REG_RIP];

    message += std::to_string((uintptr_t)addr);
    message += "\n";

    // get lib from addr
    Dl_info info;
    if (dladdr(addr, &info)) {
        message += info.dli_fname;
        message += " ";
    }

    // get line from addr
    char buf[1024];
    snprintf(buf, sizeof(buf), "addr2line -e %s %p", info.dli_fname,
             (void *)((char *)addr - (char *)info.dli_fbase));
    auto fp = utility::make_popen(buf, "r");
    if (fp) {
        char *line = nullptr;
        size_t len = 0;
        ssize_t read;
        while ((read = getline(&line, &len, fp.get())) != -1) {
            message += line;
        }
    }

    // printf("addr: %p  base: %p %d %p\n", addr, info.dli_fbase, getpid(),
    // (void*)((char*)addr - (char*)info.dli_fbase));

    message += "Offset: ";
    message +=
        std::to_string((uintptr_t)((char *)addr - (char *)info.dli_fbase));
    message += "\n";

    // get instruction
    char instr[1024];
    snprintf(instr, sizeof(instr),
             "objdump -d %s -j .text -l --start-address=%p --stop-address=%p",
             info.dli_fname, (void *)((char *)addr - (char *)info.dli_fbase),
             (void *)((char *)addr - (char *)info.dli_fbase + 1));
    auto fp2 = utility::make_popen(instr, "r");
    if (fp2) {
        char *line = nullptr;
        size_t len = 0;
        ssize_t read;
        while ((read = getline(&line, &len, fp2.get())) != -1) {
            message += line;
        }
    }

    class uniqueptr_free {
      public:
        // NOLINTNEXTLINE
        void operator()(void *ptr) {
            free(ptr); // NOLINT(hicpp-no-malloc)
        }
    };

    // get stack
    message += "Stack:\n";
    using bt_syms_t = std::unique_ptr<char *, uniqueptr_free>;

    std::array<void *, 1024> bt{};
    bt_syms_t bt_syms;
    int bt_size;

    bt_size = backtrace(bt.data(), bt.size());
    bt_syms = bt_syms_t(backtrace_symbols(bt.data(), bt_size));

    size_t fulllen = 0;
    for (int i = 1; i < bt_size; i++) {
        fulllen += strlen((bt_syms.get())[i]) + 1;
    }

    message.reserve(fulllen + message.size() + 1);

    for (int i = 1; i < bt_size; i++) {
        message.append((bt_syms.get())[i]);
        message += "\n";
    }

    notifyError("Segmentation fault", message);

    // press a key to continue
    std::cout << "Press a key to continue...";
    std::cin.get();
    exit(1);
}

int main(int argc, char **argv) {
    // segvcatch::init_segv(&handle_segv);
    // segvcatch::init_fpe(&handle_fpe);

    initNotifications("cpprepl");
    initRepl();

    bool enableSignalHandlers = false;
    std::string scriptFile;
    int localVerbosityLevel =
        0; // 0 = quiet (errors only), 1+ = increasing verbosity

    // Definir opções longas
    static struct option long_options[] = {{"help", no_argument, 0, 'h'},
                                           {"version", no_argument, 0, 'V'},
                                           {"safe", no_argument, 0, 's'},
                                           {"run", required_argument, 0, 'r'},
                                           {"verbose", no_argument, 0, 'v'},
                                           {"quiet", no_argument, 0, 'q'},
                                           {0, 0, 0, 0}};

    int c;
    int option_index = 0;
    while ((c = getopt_long(argc, argv, "hVsqr:v", long_options,
                            &option_index)) != -1) {
        switch (c) {
        case 'h': {
            showUsage(argv[0]);
            return 0;
        } break;
        case 'V': {
            showVersion();
            return 0;
        } break;
        case 's': {
            enableSignalHandlers = true;
            if (localVerbosityLevel >= 1) {
                std::cout << std::format(
                    "✅ Signal handlers enabled - crash protection active\n");
            }
        } break;
        case 'r': {
            scriptFile = optarg;
        } break;
        case 'v': {
            localVerbosityLevel++;
        } break;
        case 'q': {
            localVerbosityLevel = 0;
        } break;
        case '?': {
            std::cerr << std::format("Unknown option: -{}\n",
                                     static_cast<char>(optopt));
            std::cerr << std::format("Use '{} --help' for usage information.\n",
                                     argv[0]);
            return 1;
        } break;
        }
    }

    // Set global verbosity level for REPL system
    extern int verbosityLevel;
    verbosityLevel = localVerbosityLevel;

    // Apply signal handlers if requested
    if (enableSignalHandlers) {
        segvcatch::init_segv(&handle_segv);
        segvcatch::init_fpe(&handle_fpe);
        installCtrlCHandler();
        if (localVerbosityLevel >= 2) {
            std::cout << std::format(
                "🛡️ Hardware exception protection initialized\n");
        }
    }

    // Execute script if provided
    if (!scriptFile.empty()) {
        std::fstream file(scriptFile, std::ios::in);

        if (!file.is_open()) {
            std::cerr << std::format("❌ Error: Cannot open script file '{}'\n",
                                     scriptFile);
            std::cerr << std::format(
                "   Please check if the file exists and is readable.\n");
            return 1;
        }

        if (localVerbosityLevel >= 1) {
            std::cout << std::format("📄 Executing script: {}\n", scriptFile);
        }

        std::string line;
        int lineNumber = 0;
        std::string currentBlock; // Acumula blocos multilinhas
        int braceCount = 0;
        int parenCount = 0;
        bool insideMultilineBlock = false;

        try {
            while (std::getline(file, line)) {
                lineNumber++;

                // Skip empty lines and comments only if not inside a block
                if (!insideMultilineBlock &&
                    (line.empty() || line.starts_with("//"))) {
                    continue;
                }

                // Processar linha atual
                std::string trimmedLine = line;
                // Remove espaços no início e fim
                trimmedLine.erase(0, trimmedLine.find_first_not_of(" \t"));
                trimmedLine.erase(trimmedLine.find_last_not_of(" \t") + 1);

                // Detectar início de bloco multilinha
                if (!insideMultilineBlock) {
                    // Verifica se é início de definição multilinha
                    if (isMultilineStart(trimmedLine)) {
                        insideMultilineBlock = true;
                        currentBlock = line;
                        braceCount =
                            countChar(line, '{') - countChar(line, '}');
                        parenCount =
                            countChar(line, '(') - countChar(line, ')');

                        if (localVerbosityLevel >= 3) {
                            std::cout << std::format(
                                "🔄 Starting multiline block at line {}\n",
                                lineNumber);
                        }

                        // Se a linha já está balanceada, processa imediatamente
                        if (braceCount == 0 && parenCount == 0 &&
                            isCompleteStatement(trimmedLine)) {
                            insideMultilineBlock = false;

                            if (localVerbosityLevel >= 2) {
                                std::cout << std::format(
                                    ":{}: {}\n", lineNumber, currentBlock);
                            }

                            if (!extExecRepl(currentBlock)) {
                                if (localVerbosityLevel >= 1) {
                                    std::cout
                                        << std::format("📋 Script execution "
                                                       "completed at line {}\n",
                                                       lineNumber);
                                }
                                break;
                            }
                            currentBlock.clear();
                        }
                        continue;
                    }
                }

                if (insideMultilineBlock) {
                    // Continua acumulando o bloco
                    currentBlock += "\n" + line;
                    braceCount += countChar(line, '{') - countChar(line, '}');
                    parenCount += countChar(line, '(') - countChar(line, ')');

                    // Verifica se o bloco está completo
                    if (braceCount <= 0 && parenCount <= 0) {
                        insideMultilineBlock = false;

                        if (localVerbosityLevel >= 2) {
                            std::cout << std::format(
                                ":{}-{}: {}\n",
                                lineNumber - std::count(currentBlock.begin(),
                                                        currentBlock.end(),
                                                        '\n'),
                                lineNumber, currentBlock);
                        }

                        if (!extExecRepl(currentBlock)) {
                            if (localVerbosityLevel >= 1) {
                                std::cout
                                    << std::format("📋 Script execution "
                                                   "completed at line {}\n",
                                                   lineNumber);
                            }
                            break;
                        }
                        currentBlock.clear();
                        braceCount = 0;
                        parenCount = 0;
                    }
                } else {
                    // Linha simples - processa normalmente
                    if (localVerbosityLevel >= 2) {
                        std::cout << std::format(":{}: {}\n", lineNumber, line);
                    }

                    if (!extExecRepl(line)) {
                        if (localVerbosityLevel >= 1) {
                            std::cout << std::format(
                                "📋 Script execution completed at line {}\n",
                                lineNumber);
                        }
                        break;
                    }
                }
            }

            // Se saiu do loop mas ainda tinha um bloco incompleto
            if (insideMultilineBlock && !currentBlock.empty()) {
                if (localVerbosityLevel >= 1) {
                    std::cout << std::format("⚠️  Warning: Incomplete multiline "
                                             "block at end of script\n");
                    std::cout
                        << std::format("🔍 Block content:\n{}\n", currentBlock);
                }
            }

            if (localVerbosityLevel >= 1) {
                std::cout << std::format(
                    "✅ Script execution finished successfully\n");
            }
        } catch (const segvcatch::hardware_exception &e) {
            std::cerr << std::format("💥 Hardware exception at line {}: {}\n",
                                     lineNumber, e.what());
            if (localVerbosityLevel >= 3) {
                std::cerr << assembly_info::getInstructionAndSource(
                                 getpid(),
                                 reinterpret_cast<uintptr_t>(e.info.addr))
                          << std::endl;
            }
        } catch (const std::exception &e) {
            std::cerr << std::format("💥 C++ Exception at line {}: {}\n",
                                     lineNumber, e.what());
        }

        return 0;
    }

    // Show welcome banner for interactive mode
    if (!bootstrapProgram) {
        if (localVerbosityLevel >= 1) {
            showVersion();
            std::cout << std::format("🚀 Starting interactive mode...\n");
            std::cout << std::format(
                "💡 Type '#help' for available commands, 'exit' to quit\n\n");
        }
        repl();
    }

    try {
        if (bootstrapProgram) {
            return bootstrapProgram(argc, argv);
        }
    } catch (const std::exception &e) {
        std::cerr << "C++ exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
