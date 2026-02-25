#include <shell/IPCServer.hpp>
#include <shell/WindowManager.hpp>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <cstdlib>

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
    m_serverFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_serverFd < 0) {
        return false;
    }
    
    // Set non-blocking
    int flags = fcntl(m_serverFd, F_GETFL, 0);
    fcntl(m_serverFd, F_SETFL, flags | O_NONBLOCK);
    
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
    
    // Listen
    if (listen(m_serverFd, 5) < 0) {
        close(m_serverFd);
        m_serverFd = -1;
        unlink(socketPath.c_str());
        return false;
    }
    
    m_running = true;
    return true;
}

void IPCServer::stop() {
    if (m_clientFd >= 0) {
        close(m_clientFd);
        m_clientFd = -1;
    }
    if (m_serverFd >= 0) {
        close(m_serverFd);
        m_serverFd = -1;
    }
    if (!m_socketPath.empty()) {
        unlink(m_socketPath.c_str());
    }
    m_running = false;
}

void IPCServer::registerCommand(const std::string& cmd, CommandCallback cb) {
    CommandHandler handler;
    handler.name = cmd;
    handler.callback = cb;
    m_commands.push_back(handler);
}

void IPCServer::processMessage(const std::string& msg) {
    // Simple protocol: COMMAND [ARGS]
    // Responses: OK data or ERROR message
    
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
        if (m_clientFd >= 0) {
            write(m_clientFd, response.c_str(), response.size());
        }
    } else {
        std::string response = "ERROR Unknown command: " + cmd + "\n";
        if (m_clientFd >= 0) {
            write(m_clientFd, response.c_str(), response.size());
        }
    }
}

std::string IPCServer::handleGetWindows() {
    // Returns JSON-like list of windows
    std::ostringstream oss;
    oss << "OK [";
    
    bool first = true;
    for (const auto& w : m_windowManager.getAllWindows()) {
        if (!first) oss << ",";
        first = false;
        
        oss << "{";
        oss << "\"id\":" << w.id << ",";
        oss << "\"appId\":\"" << w.appId << "\",";
        oss << "\"title\":\"" << w.title << "\",";
        oss << "\"workspace\":" << w.workspace << ",";
        oss << "\"flags\":" << w.toFlags();
        oss << "}";
    }
    
    oss << "]\n";
    return oss.str();
}

std::string IPCServer::handleGetFocused() {
    uint64_t focused = m_windowManager.focusedWindow();
    return "OK " + std::to_string(focused) + "\n";
}

std::string IPCServer::handleFocus(const std::string& args) {
    uint64_t id = std::stoull(args);
    m_windowManager.focusWindow(id);
    return "OK\n";
}

std::string IPCServer::handleMinimize(const std::string& args) {
    uint64_t id = std::stoull(args);
    m_windowManager.minimizeWindow(id);
    return "OK\n";
}

std::string IPCServer::handleRestore(const std::string& args) {
    uint64_t id = std::stoull(args);
    m_windowManager.restoreWindow(id);
    return "OK\n";
}

std::string IPCServer::handlePin(const std::string& args) {
    // Pin app - would persist to config file
    // For now just acknowledge
    return "OK\n";
}

std::string IPCServer::handleUnpin(const std::string& args) {
    // Unpin app
    return "OK\n";
}

std::string IPCServer::handlePanelHide(const std::string& args) {
    // Hide panel - would signal panel to hide
    return "OK\n";
}

std::string IPCServer::handlePanelShow(const std::string& args) {
    // Show panel
    return "OK\n";
}

std::string IPCServer::handlePanelOpacity(const std::string& args) {
    // Set panel opacity (0-100)
    int opacity = std::atoi(args.c_str());
    if (opacity < 0 || opacity > 100) {
        return "ERROR Invalid opacity value\n";
    }
    return "OK\n";
}

std::string IPCServer::handlePanelRestart(const std::string& args) {
    // Restart panel - would respawn panel process
    return "OK\n";
}

std::string IPCServer::handleLauncher(const std::string& args) {
    // Trigger launcher - would signal panel to show launcher
    return "OK\n";
}

} // namespace havel
