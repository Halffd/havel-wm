// havel-repl - interactive Havel interpreter over the compositor IPC.
//
// Connects to the compositor socket (argument, $HAVEL_IPC_SOCKET, or
// $XDG_RUNTIME_DIR/havel-wm.sock) and evaluates each line through the
// havel_eval IPC command, printing the serialized result value.
//
//   havel> 1 + 2
//   3
//   havel> get_workspace()
//   0
//   havel> :quit
//
// Line editing is the terminal's own (raw stdin); '\\' at the end of a
// line continues the expression. Meta commands:
//   :help   this text
//   :quit   exit (also Ctrl-D)
//
// The VM state lives in the compositor - variables, keybinds and
// settings set here persist in the running session.

#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <iostream>

namespace {

int connectSocket(const std::string& path) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) {
        close(fd);
        return -1;
    }
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

// One JSON-RPC round trip: send request, read until newline, return result.
std::string rpcCall(int fd, const std::string& method, const nlohmann::json& params,
                    bool* ok) {
    nlohmann::json req = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", method},
                          {"params", params}};
    std::string out = req.dump() + "\n";
    if (send(fd, out.data(), out.size(), 0) != (ssize_t)out.size()) {
        *ok = false;
        return "connection lost";
    }
    std::string buf;
    char chunk[4096];
    while (buf.find('\n') == std::string::npos) {
        ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
        if (n <= 0) {
            *ok = false;
            return "connection closed";
        }
        buf.append(chunk, (size_t)n);
    }
    try {
        auto resp = nlohmann::json::parse(buf.substr(0, buf.find('\n')));
        *ok = true;
        if (resp.contains("result")) return resp["result"].dump(2);
        if (resp.contains("error"))
            return "error: " + resp["error"].value("message", "unknown");
    } catch (const std::exception& e) {
        *ok = false;
        return std::string("bad response: ") + e.what();
    }
    *ok = false;
    return "no result";
}

void printHelp() {
    printf(
        "meta commands:\n"
        "  :help              this text\n"
        "  :quit | :q         exit the repl\n"
        "  '\\' at line end   continue the expression on the next line\n"
        "state lives in the compositor: variables, keybinds and settings\n"
        "persist for the session; compositor functions (get_windows,\n"
        "zoom, settings, ...) are all callable from here.\n");
}

} // namespace

int main(int argc, char** argv) {
    std::string sock;
    if (argc > 1) {
        sock = argv[1];
    } else if (const char* env = getenv("HAVEL_IPC_SOCKET"); env && *env) {
        sock = env;
    } else {
        const char* xdg = getenv("XDG_RUNTIME_DIR");
        const char* home = getenv("HOME");
        if (xdg && *xdg) {
            sock = std::string(xdg) + "/havel-wm.sock";
        } else if (home && *home) {
            sock = std::string(home) + "/.havel-wm.sock";
        }
    }
    if (sock.empty()) {
        fprintf(stderr, "havel-repl: no socket path (argument, HAVEL_IPC_SOCKET or XDG_RUNTIME_DIR)\n");
        return 2;
    }

    int fd = connectSocket(sock);
    if (fd < 0) {
        fprintf(stderr, "havel-repl: cannot connect to %s (compositor running?)\n", sock.c_str());
        return 2;
    }
    printf("havel repl on %s (:help for commands)\n", sock.c_str());

    std::string line;
    std::string pending;
    bool firstLine = true;
    while (true) {
        fputs(pending.empty() ? (firstLine ? "havel> " : "havel> ")
                              : "  ....> ",
              stdout);
        fflush(stdout);
        firstLine = false;
        if (!std::getline(std::cin, line)) break;

        std::string input = pending.empty() ? line : pending + "\n" + line;
        if (!input.empty() && input.back() == '\\') {
            pending = input.substr(0, input.size() - 1);
            continue;
        }
        pending.clear();

        // trim
        while (!input.empty() && (input.back() == ' ' || input.back() == '\t' || input.back() == '\r'))
            input.pop_back();
        if (input.empty()) continue;

        if (input == ":quit" || input == ":q") break;
        if (input == ":help") {
            printHelp();
            continue;
        }

        bool ok = false;
        std::string result = rpcCall(fd, "havel_eval", {{"source", input}}, &ok);
        // havel_eval answers {"success":true,"result":"<value>"}: print
        // the value itself, not the envelope. Errors arrive via rpcCall
        // as "error: ..." text already.
        if (ok) {
            try {
                auto obj = nlohmann::json::parse(result);
                if (obj.contains("result") && obj["result"].is_string()) {
                    result = obj["result"].get<std::string>();
                }
            } catch (...) {
                // leave as-is
            }
        }
        printf("%s\n", result.c_str());
    }

    close(fd);
    printf("bye\n");
    return 0;
}
