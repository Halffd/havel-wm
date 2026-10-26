#include <shell/IPCServer.hpp>
#include <shell/WindowManager.hpp>
#include <wm/bridge.h>
#include <nlohmann/json.hpp>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <sys/wait.h>
#include <libgen.h>
#include <limits.h>

using json = nlohmann::json;

namespace {
    // Validate path: no directory traversal, no shell metacharacters
    bool isSafePath(const std::string& path) {
        if (path.empty()) return false;
        // Check for path traversal
        if (path.find("..") != std::string::npos) return false;
        // Check for shell metacharacters
        static const char* bad_chars = "|&;$(){}[]<>*'\"\\`\n\r";
        if (path.find_first_of(bad_chars) != std::string::npos) return false;
        // Must be absolute or relative without traversal
        return true;
    }

    // Validate geometry string for grim/slurp (format: "x,y wxh" or "x,y,w,h")
    bool isSafeGeometry(const std::string& geom) {
        if (geom.empty()) return true;  // Empty means use slurp interactively
        for (char c : geom) {
            if (!isdigit(c) && c != ',' && c != 'x' && c != ' ' && c != '-') {
                return false;
            }
        }
        return true;
    }

    // Safe execvp wrapper: forks, execs, returns PID or -1 on error
    pid_t safeExec(const std::vector<std::string>& argv) {
        if (argv.empty()) return -1;
        
        std::vector<char*> cargv;
        cargv.reserve(argv.size() + 1);
        for (const auto& s : argv) cargv.push_back(const_cast<char*>(s.c_str()));
        cargv.push_back(nullptr);
        
        pid_t pid = fork();
        if (pid == 0) {
            execvp(cargv[0], cargv.data());
            _exit(127);
        }
        return pid;
    }

    // Wait for process and capture output
    int waitForProcess(pid_t pid, std::string* output = nullptr) {
        int status;
        waitpid(pid, &status, 0);
        if (output) {
            // Note: We can't easily capture child output with fork/exec without pipes
            // For now, we rely on the tools writing to their output files
        }
        return WEXITSTATUS(status);
    }
} // anonymous namespace

namespace havel {

IPCServer::IPCServer(WindowManager& windowManager)
    : m_windowManager(windowManager)
{
}

IPCServer::~IPCServer() {
    stop();
}

bool IPCServer::start(const std::string& socketPath) {
    if (m_running) return false;

    // NOTE: m_socketPath is only recorded once this instance actually
    // OWNS the socket (after bind). It used to be set up front, so a
    // second compositor that failed the live-socket check below still
    // unlinked the FIRST compositor's live socket on shutdown - the
    // parity harness killed its second instance and the main
    // compositor's IPC silently vanished (phase 8 then talked to a
    // dead socket path and skipped).

    // Live-socket check: if another compositor's IPC is listening, FAIL
    // instead of stealing its socket (the old unconditional unlink broke
    // the first instance's IPC clients - visible in the parity harness
    // where a second compositor silently took over havel-wm.sock).
    // A stale socket file (dead process) is still removed.
    struct sockaddr_un probe;
    memset(&probe, 0, sizeof(probe));
    probe.sun_family = AF_UNIX;
    strncpy(probe.sun_path, socketPath.c_str(), sizeof(probe.sun_path) - 1);
    int probeFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (probeFd >= 0) {
        int connected = connect(probeFd, (struct sockaddr*)&probe, sizeof(probe));
        close(probeFd);
        if (connected == 0) {
            printf("[IPC] %s is live - another compositor owns it; not stealing\n",
                   socketPath.c_str());
            return false;
        }
    }
    unlink(socketPath.c_str());

    // Create socket
    m_serverFd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (m_serverFd < 0) {
        return false;
    }

    // Set close-on-exec
    int flags = fcntl(m_serverFd, F_GETFD, 0);
    fcntl(m_serverFd, F_SETFD, flags | FD_CLOEXEC);

    // Bind
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socketPath.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(m_serverFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(m_serverFd);
        m_serverFd = -1;
        return false;
    }

    // Set permissions (rw-rw-rw-)
    chmod(socketPath.c_str(), 0666);

    // Listen
    if (listen(m_serverFd, 16) < 0) {
        close(m_serverFd);
        m_serverFd = -1;
        unlink(socketPath.c_str());
        return false;
    }

    m_socketPath = socketPath;  // owned from here on

    m_running = true;
    // Register the listen fd with the wlr bridge's event-loop sources.
    // No-op before the display loop exists (first boot collects it via
    // havel_cpp_ipc_fds); on reloads this is what keeps IPC alive.
    havel_wlr_notify_ipc_fd(m_serverFd, 1);
    printf("[IPC] Server started at %s\n", socketPath.c_str());
    return true;
}

void IPCServer::stop() {
    // Close all clients. Every fd watched by the wlr bridge's event-loop
    // sources must be un-registered BEFORE close: a missed notify leaves
    // a stale source on the fd number, and after fd reuse it swallows
    // the next owner's events (observed live with a wayland client).
    for (int fd : m_clientFds) {
        if (fd >= 0) {
            havel_wlr_notify_ipc_fd(fd, 0);
            close(fd);
        }
    }
    m_clientFds.clear();
    m_clientBuffers.clear();
    m_clientSubscriptions.clear();

    if (m_serverFd >= 0) {
        havel_wlr_notify_ipc_fd(m_serverFd, 0);
        close(m_serverFd);
        m_serverFd = -1;
    }
    if (!m_socketPath.empty()) {
        unlink(m_socketPath.c_str());
    }
    m_running = false;
    printf("[IPC] Server stopped\n");
}

void IPCServer::registerCommand(const std::string& cmd, CommandCallback cb) {
    CommandHandler handler;
    handler.name = cmd;
    handler.callback = cb;
    m_commands.push_back(handler);
    printf("[IPC] Registered command: %s\n", cmd.c_str());
}

void IPCServer::processEvents() {
    if (!m_running || m_serverFd < 0) return;

    // Accept new client connections
    struct sockaddr_un clientAddr;
    socklen_t clientLen = sizeof(clientAddr);
    
    while (true) {
        int clientFd = accept(m_serverFd, (struct sockaddr*)&clientAddr, &clientLen);
        if (clientFd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;  // No more pending connections
            }
            break;
        }

        // Set non-blocking
        int flags = fcntl(clientFd, F_GETFL, 0);
        fcntl(clientFd, F_SETFL, flags | O_NONBLOCK);

        // Set close-on-exec
        flags = fcntl(clientFd, F_GETFD, 0);
        fcntl(clientFd, F_SETFD, flags | FD_CLOEXEC);

        m_clientFds.push_back(clientFd);
        m_clientBuffers.push_back("");
        havel_wlr_notify_ipc_fd(clientFd, 1);
        printf("[IPC] Client connected (fd=%d, total=%zu)\n", clientFd, m_clientFds.size());
    }

    // Phase 1: read from all clients and extract complete messages into
    // LOCAL copies. No dispatch here, so references into the vectors are
    // safe within this phase.
    std::vector<std::pair<int, std::string>> pending;
    for (size_t i = 0; i < m_clientFds.size(); ) {
        int clientFd = m_clientFds[i];
        if (clientFd < 0) {
            // Clean up stale entry
            m_clientFds.erase(m_clientFds.begin() + i);
            m_clientBuffers.erase(m_clientBuffers.begin() + i);
            m_clientSubscriptions.erase(clientFd);
            continue;
        }

        // Read data
        char buffer[4096];
        ssize_t n = read(clientFd, buffer, sizeof(buffer) - 1);

        if (n > 0) {
            buffer[n] = '\0';
            m_clientBuffers[i] += std::string(buffer);

            // Extract all complete messages (newline-delimited)
            std::string& buf = m_clientBuffers[i];
            size_t pos;
            while ((pos = buf.find('\n')) != std::string::npos) {
                pending.emplace_back(clientFd, buf.substr(0, pos));
                buf.erase(0, pos + 1);
            }
            i++;
        } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            // Client disconnected or error
            printf("[IPC] Client disconnected (fd=%d)\n", clientFd);
            havel_wlr_notify_ipc_fd(clientFd, 0);
            close(clientFd);
            m_clientFds.erase(m_clientFds.begin() + i);
            m_clientBuffers.erase(m_clientBuffers.begin() + i);
            m_clientSubscriptions.erase(clientFd);
            continue;
        } else {
            i++;  // EAGAIN - nothing to read
        }
    }

    // Phase 2: dispatch. processMessage can mutate the client vectors
    // (broadcastEvent closes clients on write failure), which is why no
    // references may be held across these calls - ASAN caught exactly
    // this as a heap-use-after-free (buf.find on a freed buffer).
    // Writes to a since-closed fd just fail; harmless.
    for (const auto& [fd, msg] : pending) {
        processMessage(fd, msg);
    }
}

void IPCServer::processMessage(int clientFd, const std::string& msg) {
    if (msg.empty()) return;

    // Track the requesting client for per-client commands (subscribe/
    // unsubscribe). The frame loop is single-threaded, so a plain member
    // is race-free; it is cleared after every dispatch.
    m_dispatchingFd = clientFd;

    // Check if it's a JSON-RPC request
    if (msg.find('{') != std::string::npos && msg.find('"') != std::string::npos) {
        std::string response = processJsonRequest(msg);
        m_dispatchingFd = -1;
        if (clientFd >= 0) {
            write(clientFd, response.c_str(), response.size());
        }
        return;
    }

    // Legacy text protocol: COMMAND [ARGS]
    std::istringstream iss(msg);
    std::string cmd;
    iss >> cmd;

    std::string args;
    std::getline(iss >> std::ws, args);

    // Find command handler
    auto it = std::find_if(m_commands.begin(), m_commands.end(),
        [&cmd](const CommandHandler& h) { return h.name == cmd; });

    if (it != m_commands.end()) {
        std::string response = it->callback(args);
        m_dispatchingFd = -1;
        if (clientFd >= 0) {
            write(clientFd, response.c_str(), response.size());
        }
    } else {
        m_dispatchingFd = -1;
        std::string response = "ERROR Unknown command: " + cmd + "\n";
        if (clientFd >= 0) {
            write(clientFd, response.c_str(), response.size());
        }
    }
}

std::string IPCServer::processJsonRequest(const std::string& jsonStr) {
    json j;
    try {
        j = json::parse(jsonStr);
    } catch (const json::parse_error& e) {
        return createError(0, -32700, std::string("Parse error: ") + e.what());
    }

    int id = j.value("id", 0);
    std::string method = j.value("method", "");

    if (method.empty()) {
        return createError(id, -32600, "Invalid Request: missing method");
    }

    // Extract params as JSON object
    json params = j.value("params", json::object());

    // Find command handler
    auto it = std::find_if(m_commands.begin(), m_commands.end(),
        [&method](const CommandHandler& h) { return h.name == method; });

    if (it != m_commands.end()) {
        // Exception guard: a malformed request must return an error
        // response, never kill the compositor. (Found live: `focus` with
        // any JSON-object params hit std::stoull on "{" and terminated
        // the whole session via uncaught invalid_argument.)
        std::string result;
        try {
            result = it->callback(params.dump());
        } catch (const std::exception& e) {
            return createError(id, -32000,
                               std::string("Handler error: ") + e.what());
        } catch (...) {
            return createError(id, -32000, "Handler error: unknown exception");
        }
        return createResponse(id, result);
    }

    return createError(id, -32601, "Method not found: " + method);
}

std::string IPCServer::createResponse(int id, const std::string& result) {
    // Handlers return pre-serialized JSON; parse defensively so a
    // non-JSON handler result becomes an error, not a crash.
    json resultJson;
    try {
        resultJson = json::parse(result);
    } catch (...) {
        json err = {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"error", {{"code", -32000}, {"message", "Handler returned invalid JSON"}}}
        };
        return err.dump() + "\n";
    }
    json response = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", resultJson}
    };
    return response.dump() + "\n";
}

std::string IPCServer::createError(int id, int code, const std::string& message) {
    json error = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", {{"code", code}, {"message", message}}}
    };
    return error.dump() + "\n";
}

std::string IPCServer::createEvent(const std::string& method, const std::string& params) {
    json event = {
        {"jsonrpc", "2.0"},
        {"method", method},
        {"params", json::parse(params)}
    };
    return event.dump() + "\n";
}

void IPCServer::broadcastEvent(EventType type, const std::string& jsonData) {
    std::string method = eventTypeToString(type);
    std::string event = createEvent(method, jsonData);

    for (size_t i = 0; i < m_clientFds.size(); ) {
        int fd = m_clientFds[i];
        if (fd < 0) {
            m_clientFds.erase(m_clientFds.begin() + i);
            m_clientBuffers.erase(m_clientBuffers.begin() + i);
            m_clientSubscriptions.erase(fd);
            continue;
        }

        // Check if client is subscribed to this event
        auto it = m_clientSubscriptions.find(fd);
        if (it != m_clientSubscriptions.end()) {
            if (it->second.count(type) || it->second.count(EventType::All)) {
                ssize_t n = write(fd, event.c_str(), event.size());
                if (n < 0 && errno != EAGAIN) {
                    havel_wlr_notify_ipc_fd(fd, 0);
                    close(fd);
                    m_clientFds.erase(m_clientFds.begin() + i);
                    m_clientBuffers.erase(m_clientBuffers.begin() + i);
                    m_clientSubscriptions.erase(fd);
                    continue;
                }
            }
        }
        i++;
    }
}

void IPCServer::subscribeClient(int clientFd, EventType type) {
    m_clientSubscriptions[clientFd].insert(type);
}

void IPCServer::unsubscribeClient(int clientFd, EventType type) {
    auto it = m_clientSubscriptions.find(clientFd);
    if (it != m_clientSubscriptions.end()) {
        it->second.erase(type);
    }
}

std::string IPCServer::eventTypeToString(EventType type) {
    switch (type) {
        case EventType::WindowCreated: return "window_created";
        case EventType::WindowDestroyed: return "window_destroyed";
        case EventType::WindowFocused: return "window_focused";
        case EventType::WindowTitleChanged: return "window_title_changed";
        case EventType::WindowMoved: return "window_moved";
        case EventType::WindowResized: return "window_resized";
        case EventType::WorkspaceChanged: return "workspace_changed";
        default: return "unknown_event";
    }
}

// ============================================================================
// Command Handlers
// ============================================================================

std::string IPCServer::handleGetWindows() {
    json arr = json::array();
    
    for (const auto& w : m_windowManager.getAllWindows()) {
        json win = {
            {"id", w.id},
            {"appId", w.appId},
            {"title", w.title},
            {"workspace", w.workspace},
            {"floating", hasFlag(w.flags, WindowFlags::Floating)},
            {"minimized", hasFlag(w.flags, WindowFlags::Minimized)},
            {"maximized", hasFlag(w.flags, WindowFlags::Maximized)},
            {"fullscreen", hasFlag(w.flags, WindowFlags::Fullscreen)}
        };
        arr.push_back(win);
    }
    
    return arr.dump();
}

std::string IPCServer::handleGetFocused() {
    uint64_t focused = m_windowManager.focusedWindow();
    json j = {{"id", focused}};
    return j.dump();
}

// Extract a window id from JSON params. Accepts "id" or "window_id"
// (both appear across the surface). Returns false on missing/invalid.
static bool extractWindowId(const std::string& args, uint64_t* out) {
    try {
        json j = json::parse(args);
        if (!j.is_object()) return false;
        // "id" first (historical), then "window_id"
        if (j.contains("id") && j["id"].is_number()) {
            *out = j["id"].get<uint64_t>();
            return true;
        }
        if (j.contains("window_id") && j["window_id"].is_number()) {
            *out = j["window_id"].get<uint64_t>();
            return true;
        }
        // Bare numeric param (string form)
        if (j.is_number()) {
            *out = j.get<uint64_t>();
            return true;
        }
        if (j.is_string()) {
            try {
                *out = std::stoull(j.get<std::string>());
                return true;
            } catch (...) {
                return false;
            }
        }
    } catch (...) {
        // Raw non-JSON arg that might still be a bare number
        try {
            *out = std::stoull(args);
            return true;
        } catch (...) {
            return false;
        }
    }
    return false;
}

std::string IPCServer::handleFocus(const std::string& args) {
    uint64_t id = 0;
    if (!extractWindowId(args, &id)) {
        json err = {{"error", "focus: missing/invalid window id"}};
        return err.dump();
    }
    m_windowManager.focusWindow(id);
    json j = {{"focused", id}};
    return j.dump();
}

std::string IPCServer::handleMinimize(const std::string& args) {
    uint64_t id = 0;
    if (!extractWindowId(args, &id)) {
        json err = {{"error", "minimize: missing/invalid window id"}};
        return err.dump();
    }
    m_windowManager.minimizeWindow(id);
    broadcastEvent(EventType::WindowMoved, "{\"id\":" + std::to_string(id) + ",\"minimized\":true}");
    return "{}";
}

std::string IPCServer::handleMaximize(const std::string& args) {
    uint64_t id = 0;
    if (!extractWindowId(args, &id)) {
        json err = {{"error", "maximize: missing/invalid window id"}};
        return err.dump();
    }
    m_windowManager.maximizeWindow(id);
    broadcastEvent(EventType::WindowResized, "{\"id\":" + std::to_string(id) + ",\"maximized\":true}");
    return "{}";
}

std::string IPCServer::handleRestore(const std::string& args) {
    uint64_t id = 0;
    if (!extractWindowId(args, &id)) {
        json err = {{"error", "restore: missing/invalid window id"}};
        return err.dump();
    }
    m_windowManager.restoreWindow(id);
    broadcastEvent(EventType::WindowResized, "{\"id\":" + std::to_string(id) + ",\"restored\":true}");
    return "{}";
}

std::string IPCServer::handleClose(const std::string& args) {
    uint64_t id = 0;
    if (!extractWindowId(args, &id)) {
        json err = {{"error", "close: missing/invalid window id"}};
        return err.dump();
    }
    m_windowManager.closeWindow(id);
    return "{}";
}

std::string IPCServer::handleMove(const std::string& args) {
    // params: {"id": 123, "x": 100, "y": 100}
    json j;
    try {
        j = json::parse(args);
    } catch (...) {
        json err = {{"error", "move: invalid params"}};
        return err.dump();
    }
    uint64_t id = 0;
    json idj = j.contains("id") ? j["id"] : (j.contains("window_id") ? j["window_id"] : json());
    if (idj.is_number()) id = idj.get<uint64_t>();
    int x = j.value("x", 0);
    int y = j.value("y", 0);

    m_windowManager.moveWindow(id, x, y);
    broadcastEvent(EventType::WindowMoved, args);
    return "{}";
}

std::string IPCServer::handleResize(const std::string& args) {
    // params: {"id": 123, "w": 800, "h": 600}
    json j;
    try {
        j = json::parse(args);
    } catch (...) {
        json err = {{"error", "resize: invalid params"}};
        return err.dump();
    }
    uint64_t id = 0;
    json idj = j.contains("id") ? j["id"] : (j.contains("window_id") ? j["window_id"] : json());
    if (idj.is_number()) id = idj.get<uint64_t>();
    int w = j.value("w", j.value("width", 0));
    int h = j.value("h", j.value("height", 0));

    m_windowManager.resizeWindow(id, w, h);
    broadcastEvent(EventType::WindowResized, args);
    return "{}";
}

std::string IPCServer::handleSetFloating(const std::string& args) {
    // params: {"id": 123, "floating": true}
    json j;
    try {
        j = json::parse(args);
    } catch (...) {
        json err = {{"error", "set_floating: invalid params"}};
        return err.dump();
    }
    uint64_t id = 0;
    json idj = j.contains("id") ? j["id"] : (j.contains("window_id") ? j["window_id"] : json());
    if (idj.is_number()) id = idj.get<uint64_t>();
    bool floating = j.value("floating", false);

    m_windowManager.setFloating(id, floating);
    broadcastEvent(EventType::WindowMoved, args);
    return "{}";
}

std::string IPCServer::handleWorkspace(const std::string& args) {
    // params: {"workspace": 2} or just a number
    int ws = 0;
    try {
        json j = json::parse(args);
        ws = j.value("workspace", 0);
    } catch (...) {
        ws = std::atoi(args.c_str());
    }

    m_windowManager.switchToWorkspace(ws);
    broadcastEvent(EventType::WorkspaceChanged, "{\"workspace\":" + std::to_string(ws) + "}");
    return "{}";
}

std::string IPCServer::handleGetWorkspace() {
    int ws = m_windowManager.getCurrentWorkspace();
    json j = {{"workspace", ws}};
    return j.dump();
}

std::string IPCServer::handleSpawn(const std::string& args) {
    // params: {"cmd": "foot"} or just the command string
    std::string cmd;
    try {
        json j = json::parse(args);
        cmd = j.value("cmd", "");
    } catch (...) {
        cmd = args;
        if (!cmd.empty() && cmd.front() == '"') cmd.erase(0, 1);
        if (!cmd.empty() && cmd.back() == '"') cmd.pop_back();
    }

    if (cmd.empty()) {
        json err = {{"error", "No command specified"}};
        return err.dump();
    }

    // Parse command into argv (simple space-splitting, no shell)
    std::vector<std::string> argv;
    std::stringstream ss(cmd);
    std::string token;
    while (ss >> token) {
        argv.push_back(token);
    }
    if (argv.empty()) {
        json err = {{"error", "No command specified"}};
        return err.dump();
    }

    // Convert to char* array for execvp
    std::vector<char*> cargv;
    cargv.reserve(argv.size() + 1);
    for (auto& s : argv) cargv.push_back(const_cast<char*>(s.c_str()));
    cargv.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        json err = {{"error", "fork failed"}};
        return err.dump();
    }
    if (pid == 0) {
        // Child: exec directly, no shell
        execvp(cargv[0], cargv.data());
        _exit(127);
    }

    json result = {{"spawned", cmd}, {"pid", static_cast<int>(pid)}};
    return result.dump();
}

std::string IPCServer::handleQuit() {
    // Signal compositor to quit via C bridge function
    havel_wlr_quit();
    json j = {{"quitting", true}};
    return j.dump();
}

std::string IPCServer::handlePing() {
    json j = {{"pong", true}};
    return j.dump();
}

// Map an event name from subscribe params to an EventType. Returns
// All for the special "all" name, None if unrecognized.
static IPCServer::EventType eventTypeFromName(const std::string& name) {
    using ET = IPCServer::EventType;
    if (name == "all") return ET::All;
    if (name == "window_created") return ET::WindowCreated;
    if (name == "window_destroyed") return ET::WindowDestroyed;
    if (name == "window_focused") return ET::WindowFocused;
    if (name == "window_title_changed") return ET::WindowTitleChanged;
    if (name == "window_moved") return ET::WindowMoved;
    if (name == "window_resized") return ET::WindowResized;
    if (name == "workspace_changed") return ET::WorkspaceChanged;
    return ET::None;
}

std::string IPCServer::handleSubscribe(const std::string& args) {
    // Subscribe the REQUESTING client only (m_dispatchingFd, set by
    // processMessage). Previously this looped over every connected
    // client, so one client's subscribe made all clients receive
    // broadcasts - visible live as responses desync'd by event frames
    // arriving on sockets that never subscribed.
    if (m_dispatchingFd < 0) {
        return "{\"error\":\"subscribe: no requesting client\"}";
    }
    auto& subs = m_clientSubscriptions[m_dispatchingFd];

    json j;
    bool parsed = false;
    try {
        j = json::parse(args);
        parsed = true;
    } catch (...) {
        parsed = false;
    }

    // No/invalid params or {"event": "all"} style: subscribe to everything
    if (!parsed || !j.is_object() || !j.contains("events")) {
        if (parsed && j.is_object() && j.contains("event") && j["event"].is_string()) {
            EventType t = eventTypeFromName(j["event"].get<std::string>());
            if (t != EventType::None) {
                subs.insert(t);
                return "{\"subscribed\":true}";
            }
        }
        subs.insert(EventType::All);
        return "{\"subscribed\":true}";
    }

    if (!j["events"].is_array()) {
        return "{\"error\":\"subscribe: events must be an array\"}";
    }
    for (const auto& event : j["events"]) {
        if (!event.is_string()) continue;
        EventType t = eventTypeFromName(event.get<std::string>());
        if (t != EventType::None) {
            subs.insert(t);
        }
    }
    return "{\"subscribed\":true}";
}

std::string IPCServer::handleUnsubscribe(const std::string& args) {
    // Only the requesting client (see handleSubscribe)
    if (m_dispatchingFd < 0) {
        return "{\"error\":\"unsubscribe: no requesting client\"}";
    }
    auto it = m_clientSubscriptions.find(m_dispatchingFd);
    if (it == m_clientSubscriptions.end()) {
        return "{\"unsubscribed\":true}";  // nothing to remove
    }

    json j;
    bool parsed = false;
    try {
        j = json::parse(args);
        parsed = true;
    } catch (...) {
        parsed = false;
    }

    if (!parsed || !j.is_object() || (!j.contains("events") && !j.contains("event"))) {
        // Unsubscribe from everything
        m_clientSubscriptions.erase(it);
        return "{\"unsubscribed\":true}";
    }

    std::vector<std::string> names;
    if (j.contains("events") && j["events"].is_array()) {
        for (const auto& e : j["events"]) {
            if (e.is_string()) names.push_back(e.get<std::string>());
        }
    } else if (j.contains("event") && j["event"].is_string()) {
        names.push_back(j["event"].get<std::string>());
    }
    for (const auto& name : names) {
        EventType t = eventTypeFromName(name);
        if (t != EventType::None) {
            it->second.erase(t);
        }
    }
    return "{\"unsubscribed\":true}";
}

// ============================================================================
// JSON Helper Implementations
// ============================================================================

std::string IPCServer::extractJsonString(const std::string& json, const std::string& key, const std::string& defaultValue) {
    try {
        auto j = json_t::parse(json);
        if (j.contains(key) && j[key].is_string()) {
            return j[key].get<std::string>();
        }
    } catch (...) {}
    return defaultValue;
}

int IPCServer::extractJsonInt(const std::string& json, const std::string& key, int defaultValue) {
    try {
        auto j = json_t::parse(json);
        if (j.contains(key) && j[key].is_number_integer()) {
            return j[key].get<int>();
        }
    } catch (...) {}
    return defaultValue;
}

float IPCServer::extractJsonFloat(const std::string& json, const std::string& key, float defaultValue) {
    try {
        auto j = json_t::parse(json);
        if (j.contains(key)) {
            return j[key].get<float>();
        }
    } catch (...) {}
    return defaultValue;
}

bool IPCServer::extractJsonBool(const std::string& json, const std::string& key, bool defaultValue) {
    try {
        auto j = json_t::parse(json);
        if (j.contains(key)) {
            return j[key].get<bool>();
        }
    } catch (...) {}
    return defaultValue;
}


// ============================================================================
// Screenshot & Recording Implementations
// ============================================================================

std::string IPCServer::handleScreenshot(const std::string& args) {
    json j;
    try {
        j = json::parse(args);
    } catch (...) {
        return createError(0, -1, "Invalid JSON");
    }
    
    std::string path = "~/screenshot.png";
    bool fullscreen = true;
    
    if (j.contains("path")) {
        path = j["path"].get<std::string>();
    }
    if (j.contains("fullscreen")) {
        fullscreen = j["fullscreen"].get<bool>();
    }
    
    // Expand ~ to home directory
    if (!path.empty() && path[0] == '~') {
        const char* home = getenv("HOME");
        if (home) {
            path = std::string(home) + path.substr(1);
        }
    }
    
    // Validate path
    if (!isSafePath(path)) {
        return createError(0, -2, "Invalid path");
    }
    
    std::vector<std::string> argv = {"grim"};
    if (!fullscreen) {
        std::string geometry = extractJsonString(args, "geometry", "");
        if (!geometry.empty()) {
            if (!isSafeGeometry(geometry)) {
                return createError(0, -2, "Invalid geometry");
            }
            argv.push_back("-g");
            argv.push_back(geometry);
        }
    }
    argv.push_back(path);
    
    pid_t pid = safeExec(argv);
    if (pid < 0) {
        return createError(0, -2, "Failed to execute grim");
    }
    
    int status = waitForProcess(pid);
    if (status == 0) {
        json result;
        result["success"] = true;
        result["path"] = path;
        result["message"] = "Screenshot saved";
        return result.dump();
    } else {
        return createError(0, -3, "grim failed with exit code " + std::to_string(status));
    }
}

std::string IPCServer::handleScreenshotWindow(const std::string& args) {
    json j;
    try {
        j = json::parse(args);
    } catch (...) {
        return createError(0, -1, "Invalid JSON");
    }
    
    std::string path = "~/window.png";
    if (j.contains("path")) {
        path = j["path"].get<std::string>();
    }
    
    // Expand ~ to home directory
    if (!path.empty() && path[0] == '~') {
        const char* home = getenv("HOME");
        if (home) {
            path = std::string(home) + path.substr(1);
        }
    }
    
    if (!isSafePath(path)) {
        return createError(0, -2, "Invalid path");
    }
    
    std::vector<std::string> argv = {"grim", path};
    
    pid_t pid = safeExec(argv);
    if (pid < 0) {
        return createError(0, -2, "Failed to execute grim");
    }
    
    int status = waitForProcess(pid);
    if (status == 0) {
        json result;
        result["success"] = true;
        result["path"] = path;
        result["message"] = "Window screenshot saved";
        return result.dump();
    } else {
        return createError(0, -3, "grim failed with exit code " + std::to_string(status));
    }
}

std::string IPCServer::handleScreenshotRegion(const std::string& args) {
    json j;
    try {
        j = json::parse(args);
    } catch (...) {
        return createError(0, -1, "Invalid JSON");
    }
    
    std::string path = "~/region.png";
    if (j.contains("path")) {
        path = j["path"].get<std::string>();
    }
    
    // Expand ~ to home directory
    if (!path.empty() && path[0] == '~') {
        const char* home = getenv("HOME");
        if (home) {
            path = std::string(home) + path.substr(1);
        }
    }
    
    if (!isSafePath(path)) {
        return createError(0, -2, "Invalid path");
    }
    
    std::string geometry = extractJsonString(args, "geometry", "");
    
    std::vector<std::string> argv = {"grim"};
    if (!geometry.empty()) {
        if (!isSafeGeometry(geometry)) {
            return createError(0, -2, "Invalid geometry");
        }
        argv.push_back("-g");
        argv.push_back(geometry);
    } else {
        // No geometry provided - cannot use slurp pipe safely without shell
        // Return error asking for explicit geometry
        return createError(0, -2, "Geometry required for region screenshot (use slurp separately to get geometry)");
    }
    argv.push_back(path);
    
    pid_t pid = safeExec(argv);
    if (pid < 0) {
        return createError(0, -2, "Failed to execute grim");
    }
    
    int status = waitForProcess(pid);
    if (status == 0) {
        json result;
        result["success"] = true;
        result["path"] = path;
        result["message"] = "Region screenshot saved";
        return result.dump();
    } else {
        return createError(0, -3, "grim failed with exit code " + std::to_string(status));
    }
}

std::string IPCServer::handleAudioRecord(const std::string& args) {
    json j;
    try {
        j = json::parse(args);
    } catch (...) {
        return createError(0, -1, "Invalid JSON");
    }
    
    std::string path = "~/recording.wav";
    int duration = extractJsonInt(args, "duration", 0);
    std::string source = extractJsonString(args, "source", "auto");
    
    // Expand ~ to home directory
    if (!path.empty() && path[0] == '~') {
        const char* home = getenv("HOME");
        if (home) {
            path = std::string(home) + path.substr(1);
        }
    }
    
    if (!isSafePath(path)) {
        return createError(0, -2, "Invalid path");
    }
    
    if (duration < 0 || duration > 86400) {
        return createError(0, -2, "Invalid duration");
    }
    
    std::vector<std::string> argv = {"arecord", "-f", "cd", path};
    if (duration > 0) {
        argv.insert(argv.begin() + 1, "-d");
        argv.insert(argv.begin() + 2, std::to_string(duration));
    }
    
    pid_t pid = safeExec(argv);
    if (pid < 0) {
        return createError(0, -2, "Failed to start audio recording");
    }
    
    if (duration > 0) {
        int status = waitForProcess(pid);
        if (status == 0) {
            json result;
            result["success"] = true;
            result["path"] = path;
            result["message"] = "Audio recording completed";
            result["duration"] = duration;
            return result.dump();
        } else {
            return createError(0, -3, "arecord failed with exit code " + std::to_string(status));
        }
    } else {
        // Background recording - return PID immediately
        json result;
        result["success"] = true;
        result["path"] = path;
        result["message"] = "Audio recording started";
        result["pid"] = pid;
        return result.dump();
    }
}

std::string IPCServer::handleVideoRecord(const std::string& args) {
    json j;
    try {
        j = json::parse(args);
    } catch (...) {
        return createError(0, -1, "Invalid JSON");
    }
    
    std::string path = "~/recording.mp4";
    int duration = extractJsonInt(args, "duration", 0);
    std::string geometry = extractJsonString(args, "geometry", "");
    int framerate = extractJsonInt(args, "framerate", 30);
    
    // Expand ~ to home directory
    if (!path.empty() && path[0] == '~') {
        const char* home = getenv("HOME");
        if (home) {
            path = std::string(home) + path.substr(1);
        }
    }
    
    if (!isSafePath(path)) {
        return createError(0, -2, "Invalid path");
    }
    
    if (!isSafeGeometry(geometry)) {
        return createError(0, -2, "Invalid geometry");
    }
    
    if (framerate < 1 || framerate > 120) {
        return createError(0, -2, "Invalid framerate");
    }
    
    if (duration < 0 || duration > 86400) {
        return createError(0, -2, "Invalid duration");
    }
    
    std::vector<std::string> argv = {"wf-recorder", "-f", path, "--framerate", std::to_string(framerate)};
    if (!geometry.empty()) {
        argv.push_back("-g");
        argv.push_back(geometry);
    }
    if (duration > 0) {
        argv.push_back("-d");
        argv.push_back(std::to_string(duration));
    }
    
    pid_t pid = safeExec(argv);
    if (pid < 0) {
        return createError(0, -2, "Failed to start video recording");
    }
    
    if (duration > 0) {
        int status = waitForProcess(pid);
        if (status == 0) {
            json result;
            result["success"] = true;
            result["path"] = path;
            result["message"] = "Video recording completed";
            result["duration"] = duration;
            result["framerate"] = framerate;
            return result.dump();
        } else {
            return createError(0, -3, "wf-recorder failed with exit code " + std::to_string(status));
        }
    } else {
        // Background recording - return PID immediately
        json result;
        result["success"] = true;
        result["path"] = path;
        result["message"] = "Video recording started";
        result["pid"] = pid;
        result["framerate"] = framerate;
        return result.dump();
    }
}

std::string IPCServer::handleStopRecording(const std::string& args) {
    json j;
    try {
        j = json::parse(args);
    } catch (...) {
        return createError(0, -1, "Invalid JSON");
    }
    
    int pid = extractJsonInt(args, "pid", -1);
    
    if (pid <= 0) {
        return createError(0, -2, "Invalid PID");
    }
    
    if (kill(pid, SIGINT) == 0) {
        json result;
        result["success"] = true;
        result["message"] = "Recording stopped (PID: " + std::to_string(pid) + ")";
        return result.dump();
    } else {
        return createError(0, -3, "Failed to stop recording: " + std::string(strerror(errno)));
    }
}
std::string IPCServer::jsonToString(const JsonObject& obj) {
    json_t j = obj;
    return j.dump();
}

std::string IPCServer::jsonToString(const JsonArray& arr) {
    json_t j = arr;
    return j.dump();
}

std::string IPCServer::createSuccessResponse(const std::string& message) {
    json_t j;
    j["success"] = true;
    j["message"] = message;
    return j.dump();
}

std::string IPCServer::createErrorResponse(int code, const std::string& message) {
    json_t j;
    j["success"] = false;
    j["error"] = {{"code", code}, {"message", message}};
    return j.dump();
}


// ============================================================================
// Cursor Control Implementations
// ============================================================================

std::string IPCServer::handleGetCursorPosition(const std::string& args) {
    (void)args;
    json result;
    if (!m_queryCursor) {
        result["success"] = false;
        result["error"] = "cursor query not wired to the compositor";
        return result.dump();
    }
    int x = -1, y = -1;
    if (!m_queryCursor(&x, &y)) {
        result["success"] = false;
        result["error"] = "cursor query failed (compositor not ready?)";
        return result.dump();
    }
    result["success"] = true;
    result["x"] = x;
    result["y"] = y;
    return result.dump();
}

std::string IPCServer::handleWarpCursor(const std::string& args) {
    json j;
    try {
        j = json::parse(args);
    } catch (...) {
        return createError(0, -1, "Invalid JSON");
    }

    int x = extractJsonInt(args, "x", 0);
    int y = extractJsonInt(args, "y", 0);

    json result;
    if (!m_warpCursor) {
        result["success"] = false;
        result["error"] = "cursor warp not wired to the compositor";
        return result.dump();
    }
    if (!m_warpCursor(x, y)) {
        result["success"] = false;
        result["error"] = "warp failed (out of layout bounds, or compositor not ready)";
        return result.dump();
    }
    result["success"] = true;
    result["x"] = x;
    result["y"] = y;
    return result.dump();
}

std::string IPCServer::handleClickPointer(const std::string& args) {
    int button = extractJsonInt(args, "button", 0x110);  // BTN_LEFT
    bool pressed = extractJsonBool(args, "pressed", true);
    bool release = extractJsonBool(args, "release", false);
    if (release) pressed = false;

    json result;
    if (!m_clickPointer) {
        result["success"] = false;
        result["error"] = "pointer click not wired to the compositor";
        return result.dump();
    }
    if (!m_clickPointer((uint32_t)button, pressed)) {
        result["success"] = false;
        result["error"] = "pointer click failed (compositor not ready)";
        return result.dump();
    }
    result["success"] = true;
    result["button"] = button;
    result["pressed"] = pressed;
    return result.dump();
}

std::string IPCServer::handleSetCursorTheme(const std::string& args) {
    json j;
    try {
        j = json::parse(args);
    } catch (...) {
        return createError(0, -1, "Invalid JSON");
    }
    
    std::string theme = extractJsonString(args, "theme", "default");
    int size = extractJsonInt(args, "size", 24);

    if (size < 16 || size > 128) {
        return createError(0, -2, "Cursor size must be between 16 and 128");
    }
    json result;
    if (!m_cursorTheme) {
        result["success"] = false;
        result["error"] = "cursor theme not wired to the compositor";
        return result.dump();
    }
    m_cursorTheme(theme, size);
    result["success"] = true;
    result["theme"] = theme;
    result["size"] = size;
    return result.dump();
}

std::string IPCServer::handleSetCursorSize(const std::string& args) {
    json j;
    try {
        j = json::parse(args);
    } catch (...) {
        return createError(0, -1, "Invalid JSON");
    }
    
    int size = extractJsonInt(args, "size", 24);

    if (size < 16 || size > 128) {
        return createError(0, -2, "Cursor size must be between 16 and 128");
    }

    json result;
    if (!m_cursorTheme) {
        result["success"] = false;
        result["error"] = "cursor theme not wired to the compositor";
        return result.dump();
    }
    // Keep the current theme; only the size changes.
    m_cursorTheme("", size);
    result["success"] = true;
    result["size"] = size;
    return result.dump();
}

std::string IPCServer::handleHideCursor(const std::string& args) {
    (void)args;
    json result;
    if (!m_cursorVisible) {
        result["success"] = false;
        result["error"] = "cursor visibility not wired to the compositor";
        return result.dump();
    }
    m_cursorVisible(false);
    result["success"] = true;
    return result.dump();
}

std::string IPCServer::handleShowCursor(const std::string& args) {
    (void)args;
    json result;
    if (!m_cursorVisible) {
        result["success"] = false;
        result["error"] = "cursor visibility not wired to the compositor";
        return result.dump();
    }
    m_cursorVisible(true);
    result["success"] = true;
    return result.dump();
}

} // namespace havel
