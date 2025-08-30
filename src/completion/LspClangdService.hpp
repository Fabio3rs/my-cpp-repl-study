// LspClangdService.hpp
// g++ -std=c++20 -Ipath/para/nlohmann -o demo demo.cpp
#pragma once

#include "../repl.hpp"
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_set>
#include <vector>

class LspClangdService {
  public:
    using json = nlohmann::json;

    struct Options {
        std::string rootUri = "file:///tmp";
        std::string compileCommandsDir; // clangd auto-descobre se vazio
        std::vector<std::string> extraClangdArgs; // ex.: {"--log=verbose"}
        bool backgroundIndex = true;
        bool pchInMemory = true;
        std::vector<std::string>
            extraFallbackFlags; // ex.: {"-isystem/usr/include/c++/14"}
    };

    LspClangdService() = default;
    ~LspClangdService() {
        if (running_)
            shutdown();
    }

    // callback opcional: recebe o "params" do publishDiagnostics
    std::function<void(const json &params)> onDiagnostics;

    void configure(const CompilerCodeCfg &codeCfg, const BuildSettings &build,
                   const Options &opts) {
        codeCfg_ = codeCfg;
        build_ = build;
        opts_ = opts;
    }

    bool start() {
        if (running_)
            return true;

        // argv do clangd
        std::vector<std::string> args;
        if (opts_.backgroundIndex)
            args.push_back("--background-index");
        if (opts_.pchInMemory)
            args.push_back("--pch-storage=memory");
        if (!opts_.compileCommandsDir.empty())
            args.push_back(std::string("--compile-commands-dir=") +
                           opts_.compileCommandsDir);
        for (auto &a : opts_.extraClangdArgs)
            args.push_back(a);

        if (!spawnClangd_(args))
            return false;

        // initialize (fix JSON das capabilities)
        json init = {{"jsonrpc", "2.0"},
                     {"id", nextId_()},
                     {"method", "initialize"},
                     {"params",
                      {{"processId", nullptr},
                       {"rootUri", opts_.rootUri},
                       {"initializationOptions", buildInitOpts_()},
                       {"capabilities",
                        {{"textDocument",
                          {{"synchronization", {{"didSave", true}}}}}}}}}};
        send_(init);
        (void)recv_(); // reply: initialize

        // initialized
        json inited = {{"jsonrpc", "2.0"},
                       {"method", "initialized"},
                       {"params", json::object()}};
        send_(inited);

        running_ = true;
        return true;
    }

    // Abre/declara um buffer (unsaved) com conteúdo
    void openBuffer(const std::string &uri, const std::string &languageId,
                    const std::string &text, int version = 1) {
        json didOpen = {{"jsonrpc", "2.0"},
                        {"method", "textDocument/didOpen"},
                        {"params",
                         {{"textDocument",
                           {{"uri", uri},
                            {"languageId", languageId},
                            {"version", version},
                            {"text", text}}}}}};
        send_(didOpen);
    }

    // Atualiza conteúdo (didChange) — versão deve ser monotônica
    void changeBuffer(const std::string &uri, const std::string &newText,
                      int newVersion) {
        json didChange = {
            {"jsonrpc", "2.0"},
            {"method", "textDocument/didChange"},
            {"params",
             {{"textDocument", {{"uri", uri}, {"version", newVersion}}},
              {"contentChanges", json::array({{{"text", newText}}})}}}};
        send_(didChange);
    }

    // Espera publishDiagnostics para a URI (indica preamble pronto)
    // Usa poll() + deadline monotônico.
    bool waitDiagnostics(const std::string &uri, int timeout_ms) {
        return pumpUntil(
            [&](const json &msg) -> bool {
                if (!msg.contains("method"))
                    return false;
                if (msg["method"] != "textDocument/publishDiagnostics")
                    return false;
                const auto &p = msg["params"];
                return p.contains("uri") && p["uri"] == uri;
            },
            timeout_ms);
    }

    // Completion (posições 0-based)
    json completion(const std::string &uri, int line, int character) {
        json req = {{"jsonrpc", "2.0"},
                    {"id", nextId_()},
                    {"method", "textDocument/completion"},
                    {"params",
                     {{"textDocument", {{"uri", uri}}},
                      {"position", {{"line", line}, {"character", character}}},
                      {"context", {{"triggerKind", 1}}}}}};
        send_(req);
        return recv_(); // resposta do id
    }

    // Um passo do “event loop”: lê 1 mensagem (com poll timeout) e despacha.
    // Retorna true se leu algo; false no timeout.
    bool pumpOnce(int timeout_ms) {
        if (!running_)
            return false;
        struct pollfd pfd {
            proc_.out_r, POLLIN, 0
        };
        int rc = ::poll(&pfd, 1, timeout_ms);
        if (rc < 0)
            die_("poll");
        if (rc == 0)
            return false; // timeout

        json msg = recv_();

        // requests com "id" → você pode roteá-los aqui se tiver fila/promises
        if (!msg.contains("method"))
            return true;

        const std::string method = msg["method"].get<std::string>();
        if (method == "textDocument/publishDiagnostics") {
            const auto &p = msg["params"];
            if (onDiagnostics)
                onDiagnostics(p);
        }
        // outros métodos/notifications podem ser tratados aqui

        return true;
    }

    // Executa o loop até a "predicate(msg)" ser verdadeira ou estourar timeout.
    // Retorna true se a condição foi satisfeita.
    bool pumpUntil(std::function<bool(const json &)> predicate,
                   int timeout_ms) {
        using clock = std::chrono::steady_clock;
        auto deadline = clock::now() + std::chrono::milliseconds(timeout_ms);

        while (true) {
            int remain =
                (timeout_ms < 0)
                    ? -1
                    : (int)
                          std::chrono::duration_cast<std::chrono::milliseconds>(
                              deadline - clock::now())
                              .count();
            if (timeout_ms >= 0 && remain <= 0)
                return false;

            struct pollfd pfd {
                proc_.out_r, POLLIN, 0
            };
            int rc = ::poll(&pfd, 1, remain);
            if (rc < 0)
                die_("poll");
            if (rc == 0)
                return false; // timeout

            json msg = recv_();
            // despacha publishDiagnostics para callback, mesmo se não for a que
            // esperamos
            if (msg.contains("method") &&
                msg["method"] == "textDocument/publishDiagnostics") {
                const auto &p = msg["params"];
                if (onDiagnostics)
                    onDiagnostics(p);
            }

            if (predicate && predicate(msg))
                return true;
        }
    }

    // Encerramento
    void shutdown() {
        if (!running_)
            return;
        json shutdown = {{"jsonrpc", "2.0"},
                         {"id", nextId_()},
                         {"method", "shutdown"},
                         {"params", nullptr}};
        send_(shutdown);
        (void)recv_();
        json exitmsg = {
            {"jsonrpc", "2.0"}, {"method", "exit"}, {"params", json::object()}};
        send_(exitmsg);

        if (proc_.in_w >= 0) {
            close(proc_.in_w);
            proc_.in_w = -1;
        }
        if (proc_.out_r >= 0) {
            close(proc_.out_r);
            proc_.out_r = -1;
        }
        if (proc_.pid > 0) {
            int st = 0;
            waitpid(proc_.pid, &st, 0);
            proc_.pid = -1;
        }
        running_ = false;
    }

    // helper para extrair labels de completion
    static std::vector<std::string>
    extractCompletionLabels(const json &compResp, size_t limit = 50) {
        std::vector<std::string> labels;
        if (!compResp.contains("result"))
            return labels;
        const auto &R = compResp["result"];
        auto pushItems = [&](const json &items) {
            for (auto &it : items) {
                if (it.contains("label"))
                    labels.push_back(it["label"].get<std::string>());
                if (labels.size() >= limit)
                    break;
            }
        };
        if (R.is_array())
            pushItems(R);
        else if (R.is_object() && R.contains("items"))
            pushItems(R["items"]);
        return labels;
    }

  private:
    struct Proc {
        int in_w = -1;
        int out_r = -1;
        pid_t pid = -1;
    };

    CompilerCodeCfg codeCfg_;
    BuildSettings build_;
    Options opts_;
    Proc proc_;
    bool running_ = false;
    int lastId_ = 0;

    [[noreturn]] static void die_(const char *msg) {
        perror(msg);
        std::exit(1);
    }

    static void writeAll_(int fd, const char *buf, size_t n) {
        while (n) {
            ssize_t w = ::write(fd, buf, n);
            if (w < 0)
                die_("write");
            buf += w;
            n -= (size_t)w;
        }
    }
    static void writeAll_(int fd, const std::string &s) {
        writeAll_(fd, s.data(), s.size());
    }

    static std::string readExact_(int fd, size_t n) {
        std::string out(n, '\0');
        char *p = out.data();
        size_t left = n;
        while (left) {
            ssize_t r = ::read(fd, p, left);
            if (r <= 0)
                die_("read");
            p += r;
            left -= (size_t)r;
        }
        return out;
    }

    void send_(const json &j) {
        std::string body = j.dump();
        std::ostringstream os;
        os << "Content-Length: " << body.size() << "\r\n\r\n";
        writeAll_(proc_.in_w, os.str());
        writeAll_(proc_.in_w, body);
    }

    json recv_() {
        // headers até \r\n\r\n
        std::string headers;
        char ch;
        while (true) {
            ssize_t r = ::read(proc_.out_r, &ch, 1);
            if (r <= 0)
                die_("read header");
            headers.push_back(ch);
            if (headers.size() >= 4 &&
                headers.compare(headers.size() - 4, 4, "\r\n\r\n") == 0)
                break;
        }
        // Content-Length
        std::istringstream iss(headers);
        std::string line;
        size_t contentLen = 0;
        while (std::getline(iss, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                break;
            auto pos = line.find(':');
            if (pos != std::string::npos) {
                std::string name = line.substr(0, pos);
                std::string val = line.substr(pos + 1);
                while (!val.empty() &&
                       (val.front() == ' ' || val.front() == '\t'))
                    val.erase(val.begin());
                for (auto &c : name)
                    c = (char)std::tolower((unsigned char)c);
                if (name == "content-length")
                    contentLen = (size_t)std::stoul(val);
            }
        }
        if (!contentLen) {
            std::cerr << "Content-Length ausente; headers:\n"
                      << headers << "\n";
            std::exit(1);
        }

        auto body = readExact_(proc_.out_r, contentLen);
        try {
            return json::parse(body);
        } catch (const std::exception &e) {
            std::cerr << "JSON inválido: " << e.what() << "\nBody:\n"
                      << body << "\n";
            std::exit(1);
        }
    }

    bool spawnClangd_(const std::vector<std::string> &args) {
        int inPipe[2], outPipe[2];
        if (pipe(inPipe) < 0)
            die_("pipe in");
        if (pipe(outPipe) < 0)
            die_("pipe out");

        pid_t pid = fork();
        if (pid < 0)
            die_("fork");

        if (pid == 0) {
            if (dup2(inPipe[0], STDIN_FILENO) < 0)
                die_("dup2 stdin");
            if (dup2(outPipe[1], STDOUT_FILENO) < 0)
                die_("dup2 stdout");
            close(inPipe[0]);
            close(inPipe[1]);
            close(outPipe[0]);
            close(outPipe[1]);

            std::vector<char *> argv;
            argv.push_back(const_cast<char *>("clangd"));
            for (auto &s : args)
                argv.push_back(const_cast<char *>(s.c_str()));
            argv.push_back(nullptr);

            execvp("clangd", argv.data());
            perror("execvp clangd");
            _exit(127);
        }
        // parent
        close(inPipe[0]);  // vamos escrever em inPipe[1]
        close(outPipe[1]); // vamos ler de outPipe[0]
        proc_.in_w = inPipe[1];
        proc_.out_r = outPipe[0];
        proc_.pid = pid;
        return true;
    }

    int nextId_() { return ++lastId_; }

    json buildInitOpts_() const {
        std::vector<std::string> fbf;
        // linguagem
        fbf.push_back(std::string("-x") +
                      (codeCfg_.extension.empty() ? "c++" : "c++"));
        // padrão
        fbf.push_back(std::string("-std=") +
                      (codeCfg_.std.empty() ? "gnu++20" : codeCfg_.std));
        // includes
        if (codeCfg_.addIncludes) {
            for (auto &dir : build_.includeDirectories)
                fbf.push_back(std::string("-I") + dir);
        }
        // defines
        for (auto &def : build_.preprocessorDefinitions)
            fbf.push_back(std::string("-D") + def);
        // extras
        for (auto &f : opts_.extraFallbackFlags)
            fbf.push_back(f);

        json initOpts = {{"fallbackFlags", fbf}};
        if (!opts_.compileCommandsDir.empty())
            initOpts["compilationDatabasePath"] = opts_.compileCommandsDir;
        return initOpts;
    }

    // ===== Tipos que você referenciou =====
    struct CompilerCodeCfg {
        std::string compiler = "clang++";
        std::string std = "gnu++20";
        std::string extension = "cpp";
        std::string repl_name;
        std::string libraryName;
        std::string wrapperName;
        std::vector<std::string> sourcesList;
        bool analyze = true;
        bool addIncludes = true;
        bool fileWrap = true;
        bool lazyEval = false;
        bool use_cpp2 = false;
    };
    struct BuildSettings {
        std::unordered_set<std::string> linkLibraries;
        std::unordered_set<std::string> includeDirectories;
        std::unordered_set<std::string> preprocessorDefinitions;
        std::string getLinkLibrariesStr() const {
            std::string s = " -L./ ";
            for (const auto &lib : linkLibraries)
                s += " -l" + lib;
            return s;
        }
        std::string getIncludeDirectoriesStr() const {
            std::string s;
            for (const auto &dir : includeDirectories)
                s += " -I" + dir;
            return s;
        }
        std::string getPreprocessorDefinitionsStr() const {
            std::string s;
            for (const auto &def : preprocessorDefinitions)
                s += " -D" + def;
            return s;
        }
    };
};
