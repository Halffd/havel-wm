#pragma once

#include <string>
#include <vector>

// The compositor class lives in namespace havel (wm/Server.hpp); this
// header only needs the forward declaration. HavelState comes from the
// havel-lang C API (havel.h).
namespace havel { class Server; }
struct HavelState;
using havel::Server;

/**
 * HavelHost - embeds the Havel language into the compositor.
 *
 * Owns a HavelState (the Lua-style C API from the havel-lang monorepo),
 * registers compositor operations as host functions, and runs .hv
 * scripts: the startup config (config.hv) and on-demand snippets via
 * the `havel_run` IPC command.
 *
 * Execution model: scripts run synchronously on the frame thread.
 * A script that loops forever hangs the compositor — config-style
 * quick operations only (same contract as i3's config parser).
 *
 * Host functions (available as globals in scripts):
 *   spawn(cmd)              run a command (fork+exec, no shell)
 *   workspace(n)            switch to workspace n (0-based)
 *   workspace_next()        next workspace
 *   workspace_prev()        previous workspace
 *   focus(id)               focus window by id
 *   close_window(id)        close window
 *   minimize(id) / maximize(id) / restore(id)
 *   move(id, x, y) / resize(id, w, h)
 *   set_floating(id, bool)
 *   set_gamma(g) / set_temperature(kelvin) / set_brightness(b)
 *   notify(summary, body)   desktop notification
 *   get_windows()           array of {id, app_id, title, workspace}
 *   get_workspace()         current workspace index
 *   log(msg)                compositor log (INFO)
 *   cursor_mode("auto"|"software")
 *                           hardware cursor plane when available, or force
 *                           the software cursor
 *   plugin_config(plugin, key, value)
 *                           set a plugin setting at runtime and apply it
 *                           live, e.g. plugin_config("alt_tab", "columns", 2)
 *   keybind(spec, fn)       register a keybinding; fn is a Havel
 *                           closure invoked when the binding fires.
 *                           spec: "Meta+Return", "Alt+Tab", ...
 *
 * Script syntax (validated against the interpreter):
 *   keybind("Meta+Return", fn() {
 *     spawn("alacritty")
 *   })
 */
class HavelHost {
public:
    HavelHost();
    ~HavelHost();

    // Initialize with the compositor. Registers all host functions.
    bool init(Server* server);

    // Run a .hv file (startup config or on-demand). Returns false on
    // error (message retrievable via lastError()).
    bool runFile(const std::string& path);

    // Run a script source string. Returns false on error.
    bool runString(const std::string& source, const std::string& name = "snippet");

    bool isInitialized() const { return m_initialized; }
    const std::string& lastError() const { return m_lastError; }

    // Raw state access (used by the keybind host function to store and
    // later invoke Havel closures). The host owns the state.
    HavelState* state() { return m_H; }
    int m_nextCallbackId = 0;

private:
    Server* m_server = nullptr;
    HavelState* m_H = nullptr;
    bool m_initialized = false;
    std::string m_lastError;

    void registerHostFunctions();
};
