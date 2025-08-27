#include "stdafx.hpp"

#include "repl.hpp"
#include "utility/assembly_info.hpp"
#include <cstdint>
#include <exceptdefs.h>
#include <execinfo.h>
#include <format>
#include <iostream>
#include <segvcatch.h>
#include <stdexcept>
#include <string>
#include <ucontext.h>
#include <unistd.h>

using namespace std;

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
    FILE *fp = popen(buf, "r");
    if (fp) {
        char *line = nullptr;
        size_t len = 0;
        ssize_t read;
        while ((read = getline(&line, &len, fp)) != -1) {
            message += line;
        }
        pclose(fp);
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
    FILE *fp2 = popen(instr, "r");
    if (fp2) {
        char *line = nullptr;
        size_t len = 0;
        ssize_t read;
        while ((read = getline(&line, &len, fp2)) != -1) {
            message += line;
        }
        pclose(fp2);
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

    int c;
    while ((c = getopt(argc, argv, "sr:")) != -1) {
        switch (c) {
        case 's': {
            printf("Setting signal handlers\n");
            segvcatch::init_segv(&handle_segv);
            segvcatch::init_fpe(&handle_fpe);
            installCtrlCHandler();
        } break;
        case 'r': {
            std::string_view replCmdsFile(optarg);

            std::fstream file(replCmdsFile.data(), std::ios::in);

            if (!file.is_open()) {
                std::cerr << "Cannot open file: " << replCmdsFile << '\n';
                return 1;
            }

            std::string line;
            try {
                while (std::getline(file, line)) {
                    if (!extExecRepl(line)) {
                        break;
                    }
                }
            } catch (const segvcatch::hardware_exception &e) {
                std::cerr << "Segmentation fault: " << e.what() << std::endl;
                std::cerr << assembly_info::getInstructionAndSource(
                                 getpid(),
                                 reinterpret_cast<uintptr_t>(e.info.addr))
                          << std::endl;
            } catch (const std::exception &e) {
                std::cerr << "C++ Exception: " << e.what() << std::endl;
            }
        } break;
        }
    }

    if (!bootstrapProgram) {
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
