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

using json = nlohmann::json;

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

    m_socketPath = socketPath;

    // Remove existing socket file
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

    m_running = true;
    printf("[IPC] Server started at %s\n", socketPath.c_str());
    return true;
}

void IPCServer::stop() {
    // Close all clients
    for (int fd : m_clientFds) {
        if (fd >= 0) close(fd);
    }
    m_clientFds.clear();
    m_clientBuffers.clear();
    m_clientSubscriptions.clear();

    if (m_serverFd >= 0) {
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
        printf("[IPC] Client connected (fd=%d, total=%zu)\n", clientFd, m_clientFds.size());
    }

    // Process messages from all clients
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

            // Process complete messages (newline-delimited)
            std::string& buf = m_clientBuffers[i];
            size_t pos;
            while ((pos = buf.find('\n')) != std::string::npos) {
                std::string msg = buf.substr(0, pos);
                buf.erase(0, pos + 1);
                processMessage(clientFd, msg);
            }
        } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            // Client disconnected or error
            printf("[IPC] Client disconnected (fd=%d)\n", clientFd);
            close(clientFd);
            m_clientFds.erase(m_clientFds.begin() + i);
            m_clientBuffers.erase(m_clientBuffers.begin() + i);
            m_clientSubscriptions.erase(clientFd);
            continue;
        }

        i++;
    }
}

void IPCServer::processMessage(int clientFd, const std::string& msg) {
    if (msg.empty()) return;

    // Check if it's a JSON-RPC request
    if (msg.find('{') != std::string::npos && msg.find('"') != std::string::npos) {
        std::string response = processJsonRequest(msg);
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
        if (clientFd >= 0) {
            write(clientFd, response.c_str(), response.size());
        }
    } else {
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
        std::string result = it->callback(params.dump());
        return createResponse(id, result);
    }

    return createError(id, -32601, "Method not found: " + method);
}

std::string IPCServer::createResponse(int id, const std::string& result) {
    json response = {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", json::parse(result)}
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

std::string IPCServer::handleFocus(const std::string& args) {
    uint64_t id = std::stoull(args);
    m_windowManager.focusWindow(id);
    json j = {{"focused", id}};
    return j.dump();
}

std::string IPCServer::handleMinimize(const std::string& args) {
    uint64_t id = std::stoull(args);
    m_windowManager.minimizeWindow(id);
    broadcastEvent(EventType::WindowMoved, "{\"id\":" + std::to_string(id) + ",\"minimized\":true}");
    return "{}";
}

std::string IPCServer::handleMaximize(const std::string& args) {
    uint64_t id = std::stoull(args);
    // Maximize window through WindowManager
    m_windowManager.maximizeWindow(id);
    broadcastEvent(EventType::WindowResized, "{\"id\":" + std::to_string(id) + ",\"maximized\":true}");
    return "{}";
}

std::string IPCServer::handleRestore(const std::string& args) {
    uint64_t id = std::stoull(args);
    m_windowManager.restoreWindow(id);
    broadcastEvent(EventType::WindowResized, "{\"id\":" + std::to_string(id) + ",\"restored\":true}");
    return "{}";
}

std::string IPCServer::handleClose(const std::string& args) {
    uint64_t id = std::stoull(args);
    m_windowManager.closeWindow(id);
    return "{}";
}

std::string IPCServer::handleMove(const std::string& args) {
    // params: {"id": 123, "x": 100, "y": 200}
    json j = json::parse(args);
    uint64_t id = j.value("id", static_cast<uint64_t>(0));
    int x = j.value("x", 0);
    int y = j.value("y", 0);

    m_windowManager.moveWindow(id, x, y);
    broadcastEvent(EventType::WindowMoved, args);
    return "{}";
}

std::string IPCServer::handleResize(const std::string& args) {
    // params: {"id": 123, "w": 800, "h": 600}
    json j = json::parse(args);
    uint64_t id = j.value("id", static_cast<uint64_t>(0));
    int w = j.value("w", 0);
    int h = j.value("h", 0);

    m_windowManager.resizeWindow(id, w, h);
    broadcastEvent(EventType::WindowResized, args);
    return "{}";
}

std::string IPCServer::handleSetFloating(const std::string& args) {
    // params: {"id": 123, "floating": true}
    json j = json::parse(args);
    uint64_t id = j.value("id", static_cast<uint64_t>(0));
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
        // Trim quotes if present
        if (!cmd.empty() && cmd.front() == '"') cmd.erase(0, 1);
        if (!cmd.empty() && cmd.back() == '"') cmd.pop_back();
    }

    if (cmd.empty()) {
        json err = {{"error", "No command specified"}};
        return err.dump();
    }

    // Spawn command
    std::string spawnCmd = cmd + " &";
    int ret = system(spawnCmd.c_str());

    json result = {{"spawned", cmd}, {"pid", ret}};
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

std::string IPCServer::handleSubscribe(const std::string& args) {
    // params: {"events": ["window_created", "workspace_changed"]}
    json j;
    try {
        j = json::parse(args);
    } catch (...) {
        // If parsing fails, subscribe to all events
        for (int clientFd : m_clientFds) {
            m_clientSubscriptions[clientFd].insert(EventType::All);
        }
        return "{\"subscribed\":true}";
    }
    
    // Subscribe to all events for any connected client
    for (int clientFd : m_clientFds) {
        m_clientSubscriptions[clientFd].insert(EventType::All);
        
        // Parse specific events if provided
        if (j.contains("events") && j["events"].is_array()) {
            for (const auto& event : j["events"]) {
                std::string eventName = event.get<std::string>();
                if (eventName == "window_created") {
                    m_clientSubscriptions[clientFd].insert(EventType::WindowCreated);
                } else if (eventName == "window_destroyed") {
                    m_clientSubscriptions[clientFd].insert(EventType::WindowDestroyed);
                } else if (eventName == "window_focused") {
                    m_clientSubscriptions[clientFd].insert(EventType::WindowFocused);
                } else if (eventName == "window_moved") {
                    m_clientSubscriptions[clientFd].insert(EventType::WindowMoved);
                } else if (eventName == "window_resized") {
                    m_clientSubscriptions[clientFd].insert(EventType::WindowResized);
                } else if (eventName == "workspace_changed") {
                    m_clientSubscriptions[clientFd].insert(EventType::WorkspaceChanged);
                }
            }
        }
    }
    return "{\"subscribed\":true}";
}

std::string IPCServer::handleUnsubscribe(const std::string& args) {
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

} // namespace havel
