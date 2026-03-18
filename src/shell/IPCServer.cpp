#include <shell/IPCServer.hpp>
#include <shell/WindowManager.hpp>
#include <wm/bridge.h>

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

std::string IPCServer::processJsonRequest(const std::string& json) {
    // Simple JSON-RPC 2.0 parsing
    int id = extractJsonInt(json, "id", 0);
    std::string method = extractJsonString(json, "method");

    if (method.empty()) {
        return createError(id, -32600, "Invalid Request: missing method");
    }

    // Extract params as raw string
    size_t paramsPos = json.find("\"params\"");
    std::string params;
    if (paramsPos != std::string::npos) {
        size_t start = json.find('{', paramsPos);
        if (start != std::string::npos) {
            size_t end = json.find('}', start);
            if (end != std::string::npos) {
                params = json.substr(start, end - start + 1);
            }
        }
    }

    // Find command handler
    auto it = std::find_if(m_commands.begin(), m_commands.end(),
        [&method](const CommandHandler& h) { return h.name == method; });

    if (it != m_commands.end()) {
        std::string result = it->callback(params);
        return createResponse(id, result);
    }

    return createError(id, -32601, "Method not found: " + method);
}

std::string IPCServer::createResponse(int id, const std::string& result) {
    std::ostringstream oss;
    oss << "{\"jsonrpc\":\"2.0\",\"id\":" << id << ",\"result\":" << result << "}\n";
    return oss.str();
}

std::string IPCServer::createError(int id, int code, const std::string& message) {
    std::ostringstream oss;
    oss << "{\"jsonrpc\":\"2.0\",\"id\":" << id << ",\"error\":{\"code\":" << code 
        << ",\"message\":\"" << message << "\"}}\n";
    return oss.str();
}

std::string IPCServer::createEvent(const std::string& method, const std::string& params) {
    std::ostringstream oss;
    oss << "{\"jsonrpc\":\"2.0\",\"method\":\"" << method << "\",\"params\":" << params << "}\n";
    return oss.str();
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

// JSON helpers
std::string IPCServer::extractJsonString(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\"";
    size_t keyPos = json.find(searchKey);
    if (keyPos == std::string::npos) return "";

    size_t colonPos = json.find(':', keyPos);
    if (colonPos == std::string::npos) return "";

    size_t startQuote = json.find('"', colonPos);
    if (startQuote == std::string::npos) return "";

    size_t endQuote = json.find('"', startQuote + 1);
    if (endQuote == std::string::npos) return "";

    return json.substr(startQuote + 1, endQuote - startQuote - 1);
}

int IPCServer::extractJsonInt(const std::string& json, const std::string& key, int defaultValue) {
    std::string searchKey = "\"" + key + "\"";
    size_t keyPos = json.find(searchKey);
    if (keyPos == std::string::npos) return defaultValue;

    size_t colonPos = json.find(':', keyPos);
    if (colonPos == std::string::npos) return defaultValue;

    size_t numStart = colonPos + 1;
    while (numStart < json.size() && (json[numStart] == ' ' || json[numStart] == '\t')) {
        numStart++;
    }

    if (numStart >= json.size()) return defaultValue;

    char* endptr;
    long val = strtol(json.c_str() + numStart, &endptr, 10);
    if (endptr == json.c_str() + numStart) return defaultValue;

    return (int)val;
}

// ============================================================================
// Command Handlers
// ============================================================================

std::string IPCServer::handleGetWindows() {
    std::ostringstream oss;
    oss << "[";

    bool first = true;
    for (const auto& w : m_windowManager.getAllWindows()) {
        if (!first) oss << ",";
        first = false;

        oss << "{";
        oss << "\"id\":" << w.id << ",";
        oss << "\"appId\":\"" << w.appId << "\",";
        oss << "\"title\":\"" << w.title << "\",";
        oss << "\"workspace\":" << w.workspace << ",";
        oss << "\"floating\":" << (hasFlag(w.flags, WindowFlags::Floating) ? "true" : "false") << ",";
        oss << "\"minimized\":" << (hasFlag(w.flags, WindowFlags::Minimized) ? "true" : "false") << ",";
        oss << "\"maximized\":" << (hasFlag(w.flags, WindowFlags::Maximized) ? "true" : "false") << ",";
        oss << "\"fullscreen\":" << (hasFlag(w.flags, WindowFlags::Fullscreen) ? "true" : "false");
        oss << "}";
    }

    oss << "]";
    return oss.str();
}

std::string IPCServer::handleGetFocused() {
    uint64_t focused = m_windowManager.focusedWindow();
    std::ostringstream oss;
    oss << "{\"id\":" << focused << "}";
    return oss.str();
}

std::string IPCServer::handleFocus(const std::string& args) {
    uint64_t id = std::stoull(args);
    m_windowManager.focusWindow(id);
    return "{}";
}

std::string IPCServer::handleMinimize(const std::string& args) {
    uint64_t id = std::stoull(args);
    m_windowManager.minimizeWindow(id);
    broadcastEvent(EventType::WindowMoved, "{\"id\":" + std::to_string(id) + ",\"minimized\":true}");
    return "{}";
}

std::string IPCServer::handleMaximize(const std::string& args) {
    uint64_t id = std::stoull(args);
    // Stub - would need C bridge integration
    (void)id;
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
    uint64_t id = (uint64_t)extractJsonInt(args, "id", 0);
    int x = extractJsonInt(args, "x", 0);
    int y = extractJsonInt(args, "y", 0);
    
    m_windowManager.moveWindow(id, x, y);
    broadcastEvent(EventType::WindowMoved, args);
    return "{}";
}

std::string IPCServer::handleResize(const std::string& args) {
    // params: {"id": 123, "w": 800, "h": 600}
    uint64_t id = (uint64_t)extractJsonInt(args, "id", 0);
    int w = extractJsonInt(args, "w", 0);
    int h = extractJsonInt(args, "h", 0);
    
    m_windowManager.resizeWindow(id, w, h);
    broadcastEvent(EventType::WindowResized, args);
    return "{}";
}

std::string IPCServer::handleSetFloating(const std::string& args) {
    // params: {"id": 123, "floating": true}
    uint64_t id = (uint64_t)extractJsonInt(args, "id", 0);
    bool floating = args.find("\"floating\":true") != std::string::npos;
    
    m_windowManager.setFloating(id, floating);
    broadcastEvent(EventType::WindowMoved, args);
    return "{}";
}

std::string IPCServer::handleWorkspace(const std::string& args) {
    // params: {"workspace": 2} or just a number
    int ws = extractJsonInt(args, "workspace", 0);
    if (ws == 0) {
        ws = std::atoi(args.c_str());
    }
    
    m_windowManager.switchToWorkspace(ws);
    broadcastEvent(EventType::WorkspaceChanged, "{\"workspace\":" + std::to_string(ws) + "}");
    return "{}";
}

std::string IPCServer::handleGetWorkspace() {
    int ws = m_windowManager.getCurrentWorkspace();
    std::ostringstream oss;
    oss << "{\"workspace\":" << ws << "}";
    return oss.str();
}

std::string IPCServer::handleSpawn(const std::string& args) {
    // params: {"cmd": "foot"} or just the command string
    std::string cmd = extractJsonString(args, "cmd");
    if (cmd.empty()) {
        // Legacy: just use args directly
        cmd = args;
        // Trim quotes if present
        if (!cmd.empty() && cmd.front() == '"') cmd.erase(0, 1);
        if (!cmd.empty() && cmd.back() == '"') cmd.pop_back();
    }

    if (cmd.empty()) {
        return "{\"error\":\"No command specified\"}";
    }

    // Spawn command (uses system() for now - would need C bridge for proper integration)
    std::string spawnCmd = cmd + " &";
    int ret = system(spawnCmd.c_str());

    std::ostringstream oss;
    oss << "{\"spawned\":\"" << cmd << "\",\"pid\":" << ret << "}";
    return oss.str();
}

std::string IPCServer::handleQuit() {
    // Signal compositor to quit (stub - would need C bridge integration)
    return "{\"quitting\":true}";
}

std::string IPCServer::handlePing() {
    return "{\"pong\":true}";
}

std::string IPCServer::handleSubscribe(const std::string& args) {
    // params: {"events": ["window_created", "workspace_changed"]}
    // This would need client fd - handled in processMessage for now
    return "{\"subscribed\":true}";
}

std::string IPCServer::handleUnsubscribe(const std::string& args) {
    return "{\"unsubscribed\":true}";
}

} // namespace havel
