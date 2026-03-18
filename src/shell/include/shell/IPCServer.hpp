#pragma once

#include <shell/WindowManager.hpp>
#include <string>
#include <functional>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace havel {

/**
 * Enhanced IPC Server - Multiple clients, JSON protocol, event subscriptions
 *
 * Features:
 * - Multiple concurrent clients (panel, external tools, scripts)
 * - Proper JSON request/response protocol
 * - Event subscriptions (window changes, workspace changes, focus changes)
 * - Rich command set (window management, workspace, spawn, query)
 * - Proper error handling with JSON error responses
 *
 * Protocol:
 *   Request:  {"id": 1, "method": "get_windows", "params": {...}}
 *   Response: {"id": 1, "result": {...}} or {"id": 1, "error": {"code": -1, "message": "..."}}
 *
 * Events (server-pushed):
 *   {"jsonrpc": "2.0", "method": "window_created", "params": {...}}
 */
class IPCServer {
public:
    using CommandCallback = std::function<std::string(const std::string&)>;
    using EventHandler = std::function<void(const std::string&)>;

    // Event types for subscriptions
    enum class EventType {
        WindowCreated,
        WindowDestroyed,
        WindowFocused,
        WindowMoved,
        WindowResized,
        WorkspaceChanged,
        All  // Subscribe to all events
    };

    IPCServer(WindowManager& windowManager);
    ~IPCServer();

    // Start/stop IPC server
    bool start(const std::string& socketPath);
    void stop();
    bool isRunning() const { return m_running; }

    // Register command handlers
    void registerCommand(const std::string& cmd, CommandCallback cb);

    // Process pending IPC events (called from main loop)
    void processEvents();

    // Get socket path
    const std::string& getSocketPath() const { return m_socketPath; }

    // Event broadcasting
    void broadcastEvent(EventType type, const std::string& jsonData);
    void subscribeClient(int clientFd, EventType type);
    void unsubscribeClient(int clientFd, EventType type);

    // Get connected client count
    size_t getClientCount() const { return m_clientFds.size(); }

    // Command handlers (public for Server.cpp lambdas)
    std::string handleGetWindows();
    std::string handleGetFocused();
    std::string handleFocus(const std::string& args);
    std::string handleMinimize(const std::string& args);
    std::string handleMaximize(const std::string& args);
    std::string handleRestore(const std::string& args);
    std::string handleClose(const std::string& args);
    std::string handleMove(const std::string& args);
    std::string handleResize(const std::string& args);
    std::string handleSetFloating(const std::string& args);
    std::string handleWorkspace(const std::string& args);
    std::string handleGetWorkspace();
    std::string handleSpawn(const std::string& args);
    std::string handleQuit();
    std::string handlePing();
    std::string handleSubscribe(const std::string& args);
    std::string handleUnsubscribe(const std::string& args);

private:
    // Message processing
    void processMessage(int clientFd, const std::string& msg);
    std::string processJsonRequest(const std::string& json);
    std::string createResponse(int id, const std::string& result);
    std::string createError(int id, int code, const std::string& message);
    std::string createEvent(const std::string& method, const std::string& params);

    // JSON helpers (simple manual parsing - no external dependency)
    std::string extractJsonString(const std::string& json, const std::string& key);
    int extractJsonInt(const std::string& json, const std::string& key, int defaultValue = 0);

    // Event type to string
    static std::string eventTypeToString(EventType type);

    WindowManager& m_windowManager;
    std::string m_socketPath;
    int m_serverFd = -1;
    bool m_running = false;

    // Multiple clients (epoll-style with simple fd list)
    std::vector<int> m_clientFds;
    std::vector<std::string> m_clientBuffers;  // One buffer per client
    std::unordered_map<int, std::unordered_set<EventType>> m_clientSubscriptions;

    struct CommandHandler {
        std::string name;
        CommandCallback callback;
    };
    std::vector<CommandHandler> m_commands;
};

} // namespace havel
