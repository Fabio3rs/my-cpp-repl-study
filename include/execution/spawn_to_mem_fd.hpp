// spawn_to_memfd_map.hpp
#pragma once
#include "../utility/eval_scope_exit.hpp"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001
#endif
#ifndef SYS_memfd_create
#include <sys/syscall.h>
#endif

inline int memfd_create_compat(const char *name, unsigned flags) {
#ifdef SYS_memfd_create
    return static_cast<int>(::syscall(SYS_memfd_create, name, flags));
#else
    errno = ENOSYS;
    return -1;
#endif
}

namespace execution {
struct Options {
    bool redirect_stderr{false}; // válido só no modo dup2
    int memfd_flags{0};          // 0 aqui (sem CLOEXEC) para facilitar herança
};

class SpawnToMemfdMap {
  public:
    explicit SpawnToMemfdMap(const Options &opt = {})
        : m_pid(-1), m_fd(-1), m_addr(MAP_FAILED), m_len(0) {
        m_fd = memfd_create_compat("cpprepl-cap", opt.memfd_flags);
        if (m_fd < 0) {
            throw std::runtime_error(std::string("memfd_create: ") +
                                     std::strerror(errno));
        }
        m_opts = opt;
    }

    ~SpawnToMemfdMap() {
        if (m_addr != MAP_FAILED) {
            ::munmap(m_addr, m_len);
        }
        if (m_fd >= 0) {
            ::close(m_fd);
        }
    }

    // Caminho "/proc/<self>/fd/<fd>" para usar com shell:  cmd + " > " +
    // getFdPath()
    std::string getFdPath() const {
        std::string s = "/proc/self/fd/";
        s += std::to_string(m_fd);
        return s;
    }

    // Fluxo A (preferido): dup2(mfd -> stdout[,stderr]) no filho, sem shell
    int runDup2(const std::vector<std::string> &argv) {
        if (argv.empty()) {
            return EINVAL;
        }

        posix_spawn_file_actions_t acts;
        if (int rc = ::posix_spawn_file_actions_init(&acts)) {
            return rc;
        }
        auto guard =
            EvalOnScopeExit([&] { ::posix_spawn_file_actions_destroy(&acts); });

        // redireciona stdout para o memfd (dup2 no filho)
        if (int rc = ::posix_spawn_file_actions_adddup2(&acts, m_fd,
                                                        STDOUT_FILENO)) {
            return rc;
        }
        if (m_opts.redirect_stderr) {
            if (int rc = ::posix_spawn_file_actions_adddup2(&acts, m_fd,
                                                            STDERR_FILENO))
                return rc;
        }

        // argv**
        std::vector<char *> cargv;
        cargv.reserve(argv.size() + 1);
        for (auto &s : argv) {
            cargv.push_back(const_cast<char *>(s.c_str()));
        }
        cargv.push_back(nullptr);

        if (int rc = ::posix_spawnp(&m_pid, cargv[0], &acts, nullptr,
                                    cargv.data(), environ)) {
            return rc;
        }
        return wait_and_map();
    }

    // Fluxo B: você passa um comando único (ex.: "/bin/sh -c '... >
    // /proc/self/fd/FD'")
    int runPath(const std::string &command) {
        // Executa via /bin/sh -c <command>
        const char *sh = "/bin/sh";
        const char *dashc = "-c";
        char *const cargv[] = {const_cast<char *>(sh),
                               const_cast<char *>(dashc),
                               const_cast<char *>(command.c_str()), nullptr};
        if (int rc =
                ::posix_spawnp(&m_pid, sh, nullptr, nullptr, cargv, environ)) {
            return rc;
        }
        return wait_and_map();
    }

    std::string_view view() const {
        return (m_addr == MAP_FAILED)
                   ? std::string_view{}
                   : std::string_view(static_cast<const char *>(m_addr), m_len);
    }

    size_t size() const { return m_len; }
    int fd() const { return m_fd; }

  private:
    template <class F> struct Finally {
        F f;
        ~Finally() { f(); }
    };

    int wait_and_map() {
        int status = 0;
        if (::waitpid(m_pid, &status, 0) < 0) {
            return errno;
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            return (status ? status : ECHILD);
        }

        struct stat st {};
        if (::fstat(m_fd, &st) != 0) {
            return errno;
        }
        if (st.st_size == 0) {
            m_len = 0;
            return 0;
        }

        m_len = static_cast<size_t>(st.st_size);
        m_addr = ::mmap(nullptr, m_len, PROT_READ, MAP_PRIVATE, m_fd, 0);
        if (m_addr == MAP_FAILED) {
            return errno;
        }
        return 0;
    }

    Options m_opts{};
    pid_t m_pid{};
    int m_fd{};
    void *m_addr{MAP_FAILED};
    size_t m_len{};
};
} // namespace execution
