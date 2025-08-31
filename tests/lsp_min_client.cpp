// g++ -std=c++20 lsp_min_client.cpp -o lsp_min
// ./lsp_min

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

static void writeAll(int fd, const std::string &s) {
    const char *p = s.data();
    size_t n = s.size();
    while (n) {
        ssize_t w = ::write(fd, p, n);
        if (w < 0) {
            perror("write");
            std::exit(1);
        }
        p += w;
        n -= (size_t)w;
    }
}

static std::string readExact(int fd, size_t n) {
    std::string out;
    out.resize(n);
    char *p = out.data();
    size_t rleft = n;
    while (rleft) {
        ssize_t r = ::read(fd, p, rleft);
        if (r <= 0) {
            perror("read");
            std::exit(1);
        }
        p += r;
        rleft -= (size_t)r;
    }
    return out;
}

static std::string readMessage(int fd) {
    // Lê cabeçalhos até linha vazia
    std::string headers;
    char c;
    std::string line;
    int state = 0; // 0 lendo linhas; detecta \r\n\r\n
    while (true) {
        ssize_t r = ::read(fd, &c, 1);
        if (r <= 0) {
            perror("read");
            std::exit(1);
        }
        headers.push_back(c);

        size_t L = headers.size();
        if (L >= 4 && headers.substr(L - 4) == "\r\n\r\n")
            break;
    }

    // Busca Content-Length
    std::istringstream iss(headers);
    std::string hline;
    size_t contentLen = 0;
    while (std::getline(iss, hline) && hline != "\r") {
        if (hline.ends_with('\r'))
            hline.pop_back();
        auto pos = hline.find(':');
        if (pos != std::string::npos) {
            std::string name = hline.substr(0, pos);
            std::string val = hline.substr(pos + 1);
            // trim:
            while (!val.empty() && (val.front() == ' ' || val.front() == '\t'))
                val.erase(val.begin());
            if (!name.empty() &&
                strcasecmp(name.c_str(), "Content-Length") == 0) {
                contentLen = (size_t)std::stoul(val);
            }
        }
    }
    if (!contentLen) {
        std::cerr << "Sem Content-Length!\n";
        std::exit(1);
    }

    return readExact(fd, contentLen);
}

static void sendMessage(int fd, const std::string &json) {
    std::ostringstream os;
    os << "Content-Length: " << json.size() << "\r\n\r\n" << json;
    writeAll(fd, os.str());
}

int main() {
    // 1) cria pipes para stdin/stdout do clangd
    int inPipe[2], outPipe[2];
    if (pipe(inPipe) < 0) {
        perror("pipe in");
        return 1;
    }
    if (pipe(outPipe) < 0) {
        perror("pipe out");
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        // filho: conecta stdin/stdout aos pipes e exec clangd
        dup2(inPipe[0], STDIN_FILENO);
        dup2(outPipe[1], STDOUT_FILENO);
        // fecha descritores extras
        close(inPipe[0]);
        close(inPipe[1]);
        close(outPipe[0]);
        close(outPipe[1]);

        // Ajuste opções se quiser: --background-index, --compile-commands-dir,
        // etc. clangd fala LSP por stdio.
        const char *argv[] = {"clangd", "--background-index",
                              "--pch-storage=memory",
                              // "--compile-commands-dir=/caminho/projeto",
                              nullptr};
        execvp(argv[0], (char *const *)argv);
        perror("execvp clangd");
        _exit(127);
    }

    // pai: usará inPipe[1] para escrever e outPipe[0] para ler
    close(inPipe[0]);
    close(outPipe[1]);

    auto send = [&](const std::string &j) { sendMessage(inPipe[1], j); };
    auto recv = [&]() { return readMessage(outPipe[0]); };

    // 2) initialize
    std::string init = R"({
      "jsonrpc":"2.0",
      "id":1,
      "method":"initialize",
      "params":{
        "processId": null,
        "rootUri": "file:///tmp",
        "capabilities": {
          "textDocument": {
            "synchronization": {"didSave": true}
          }
        }
      }
    })";
    send(init);
    std::cout << "← " << recv() << "\n"; // initialize result

    // 3) initialized (notification)
    std::string inited =
        R"({"jsonrpc":"2.0","method":"initialized","params":{}})";
    send(inited);

    // 4) didOpen com buffer “unsaved”
    const char *code = "#include <iostream>\n"
                       "int main(){\n"
                       "  std::cou\n"
                       "}\n";
    std::ostringstream open;
    open
        << R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{)"
        << R"("uri":"file:///buffer.cpp","languageId":"cpp","version":1,"text":")";
    // escapa apenas \ e " de forma simples
    for (const char *p = code; *p; ++p) {
        if (*p == '\\' || *p == '\"')
            open << '\\';
        open << *p;
    }
    open << R"("}}})";
    send(open.str());

    // 5) completion em (linha=2, col=11) 0-based → após "std::cou"
    std::string comp = R"({
      "jsonrpc":"2.0","id":2,"method":"textDocument/completion",
      "params":{"textDocument":{"uri":"file:///buffer.cpp"},
                "position":{"line":2,"character":11},
                "context":{"triggerKind":1}}
    })";
    send(comp);
    std::string resp = recv();
    std::cout << "← " << resp << "\n";

    // encerra educadamente
    std::string shutdown =
        R"({"jsonrpc":"2.0","id":3,"method":"shutdown","params":null})";
    send(shutdown);
    std::cout << "← " << recv() << "\n";
    std::string exitmsg = R"({"jsonrpc":"2.0","method":"exit","params":{}})";
    send(exitmsg);

    close(inPipe[1]);
    close(outPipe[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    return 0;
}
