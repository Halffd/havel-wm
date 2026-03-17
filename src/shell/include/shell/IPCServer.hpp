#pragma once

#include <shell/WindowManager.hpp>
#include <string>
#include <functional>

namespace havel {

/**
 * IPC server for taskbar/panel communication
 *
 * Exposes window data via Unix domain socket.
 * Protocol: JSON-like text messages
 */
class IPCServer {
public:
    using CommandCallback = std::function<std::string(const std::string&)>;

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

    // Command handlers (public for Server.cpp lambdas)
    std::string handleGetWindows();
    std::string handleGetFocused();
    std::string handleFocus(const std::string& args);
    std::string handleMinimize(const std::string& args);
    std::string handleRestore(const std::string& args);

private:
    void processMessage(const std::string& msg);
    std::string handlePin(const std::string& args);
    std::string handleUnpin(const std::string& args);
    std::string handlePanelHide(const std::string& args);
    std::string handlePanelShow(const std::string& args);
    std::string handlePanelOpacity(const std::string& args);
    std::string handlePanelRestart(const std::string& args);
    std::string handleLauncher(const std::string& args);

    WindowManager& m_windowManager;
    std::string m_socketPath;
    int m_serverFd = -1;
    int m_clientFd = -1;
    bool m_running = false;

    struct CommandHandler {
        std::string name;
        CommandCallback callback;
    };
    std::vector<CommandHandler> m_commands;
};

} // namespace havel
