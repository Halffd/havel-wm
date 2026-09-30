// HavelHost - Havel language embedded in the compositor.

#include "HavelHost.hpp"
#include <wm/Server.hpp>
#include <wm/input/KeybindingManager.hpp>
#include <wm/plugins/PluginConfig.hpp>
#include <Logger.h>
#include <havel.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

using namespace havel;

// The compositor context passed to every host function via userdata.
struct HavelCompositorCtx {
    Server* server;
};

// ---------------------------------------------------------------------------
// Helpers: stack access in host functions
// ---------------------------------------------------------------------------

static int64_t arg_int(HavelState* H, int idx) {
    return havel_tointeger(H, idx);
}

static double arg_num(HavelState* H, int idx) {
    return havel_tonumber(H, idx);
}

static const char* arg_str(HavelState* H, int idx) {
    return havel_tostring(H, idx);
}

// ---------------------------------------------------------------------------
// Host functions: compositor operations
// ---------------------------------------------------------------------------
// Every function: args on the stack (1-based), result pushed on return.
// Exceptions are fine: havel_call catches and reports via last_error.

static int hf_spawn(HavelState* H, void*) {
    auto* ctx = (HavelCompositorCtx*)havel_getuserdata(H);
    const char* cmd = arg_str(H, 0);
    if (!cmd || !*cmd || !ctx || !ctx->server) {
        havel_pushboolean(H, 0);
        return HAVEL_OK;
    }
    ctx->server->spawnCommand(cmd);
    havel_pushboolean(H, 1);
    return HAVEL_OK;
}

static int hf_workspace(HavelState* H, void*) {
    auto* ctx = (HavelCompositorCtx*)havel_getuserdata(H);
    int64_t ws = arg_int(H, 0);
    if (ctx && ctx->server && ws >= 0) {
        ctx->server->workspaceStepTo((uint32_t)ws);
    }
    havel_pushnil(H);
    return HAVEL_OK;
}

static int hf_workspace_next(HavelState* H, void*) {
    auto* ctx = (HavelCompositorCtx*)havel_getuserdata(H);
    if (ctx && ctx->server) ctx->server->workspaceStep(false);
    havel_pushnil(H);
    return HAVEL_OK;
}

static int hf_workspace_prev(HavelState* H, void*) {
    auto* ctx = (HavelCompositorCtx*)havel_getuserdata(H);
    if (ctx && ctx->server) ctx->server->workspaceStep(true);
    havel_pushnil(H);
    return HAVEL_OK;
}

static int hf_focus(HavelState* H, void*) {
    auto* ctx = (HavelCompositorCtx*)havel_getuserdata(H);
    int64_t id = arg_int(H, 0);
    if (ctx && ctx->server && id >= 0) {
        ctx->server->windowManager().focusWindow((uint64_t)id);
    }
    havel_pushnil(H);
    return HAVEL_OK;
}

static int hf_close_window(HavelState* H, void*) {
    auto* ctx = (HavelCompositorCtx*)havel_getuserdata(H);
    int64_t id = arg_int(H, 0);
    if (ctx && ctx->server && id >= 0) {
        ctx->server->windowManager().closeWindow((uint64_t)id);
    }
    havel_pushnil(H);
    return HAVEL_OK;
}

static int hf_minimize(HavelState* H, void*) {
    auto* ctx = (HavelCompositorCtx*)havel_getuserdata(H);
    int64_t id = arg_int(H, 0);
    if (ctx && ctx->server && id >= 0) {
        ctx->server->windowManager().minimizeWindow((uint64_t)id);
    }
    havel_pushnil(H);
    return HAVEL_OK;
}

static int hf_maximize(HavelState* H, void*) {
    auto* ctx = (HavelCompositorCtx*)havel_getuserdata(H);
    int64_t id = arg_int(H, 0);
    if (ctx && ctx->server && id >= 0) {
        ctx->server->windowManager().maximizeWindow((uint64_t)id);
    }
    havel_pushnil(H);
    return HAVEL_OK;
}

static int hf_restore(HavelState* H, void*) {
    auto* ctx = (HavelCompositorCtx*)havel_getuserdata(H);
    int64_t id = arg_int(H, 0);
    if (ctx && ctx->server && id >= 0) {
        ctx->server->windowManager().restoreWindow((uint64_t)id);
    }
    havel_pushnil(H);
    return HAVEL_OK;
}

static int hf_move(HavelState* H, void*) {
    auto* ctx = (HavelCompositorCtx*)havel_getuserdata(H);
    int64_t id = arg_int(H, 0);
    int64_t x = arg_int(H, 1);
    int64_t y = arg_int(H, 2);
    if (ctx && ctx->server && id >= 0) {
        ctx->server->windowManager().moveWindow((uint64_t)id, (int)x, (int)y);
    }
    havel_pushnil(H);
    return HAVEL_OK;
}

static int hf_resize(HavelState* H, void*) {
    auto* ctx = (HavelCompositorCtx*)havel_getuserdata(H);
    int64_t id = arg_int(H, 0);
    int64_t w = arg_int(H, 1);
    int64_t h = arg_int(H, 2);
    if (ctx && ctx->server && id >= 0) {
        ctx->server->windowManager().resizeWindow((uint64_t)id, (int)w, (int)h);
    }
    havel_pushnil(H);
    return HAVEL_OK;
}

static int hf_set_floating(HavelState* H, void*) {
    auto* ctx = (HavelCompositorCtx*)havel_getuserdata(H);
    int64_t id = arg_int(H, 0);
    int floating = havel_toboolean(H, 2);
    if (ctx && ctx->server && id >= 0) {
        ctx->server->windowManager().setFloating((uint64_t)id, floating != 0);
    }
    havel_pushnil(H);
    return HAVEL_OK;
}

static int hf_set_gamma(HavelState* H, void*) {
    auto* ctx = (HavelCompositorCtx*)havel_getuserdata(H);
    double g = arg_num(H, 0);
    if (ctx && ctx->server) ctx->server->setGamma((float)g);
    havel_pushnil(H);
    return HAVEL_OK;
}

static int hf_set_temperature(HavelState* H, void*) {
    auto* ctx = (HavelCompositorCtx*)havel_getuserdata(H);
    int64_t k = arg_int(H, 0);
    if (ctx && ctx->server) ctx->server->setTemperature((int)k);
    havel_pushnil(H);
    return HAVEL_OK;
}

static int hf_set_brightness(HavelState* H, void*) {
    auto* ctx = (HavelCompositorCtx*)havel_getuserdata(H);
    double b = arg_num(H, 0);
    if (ctx && ctx->server) ctx->server->setBrightness((float)b);
    havel_pushnil(H);
    return HAVEL_OK;
}

static int hf_notify(HavelState* H, void*) {
    auto* ctx = (HavelCompositorCtx*)havel_getuserdata(H);
    const char* summary = arg_str(H, 0);
    const char* body = havel_gettop(H) >= 2 ? arg_str(H, 1) : "";
    if (ctx && ctx->server && summary && *summary) {
        // Same path the IPC notify command uses
        std::string args = "{\"summary\":\"" + std::string(summary) +
                           "\",\"body\":\"" + (body ? body : "") + "\"}";
        ctx->server->ipcNotify(args);
    }
    havel_pushnil(H);
    return HAVEL_OK;
}

static int hf_log(HavelState* H, void*) {
    const char* msg = arg_str(H, 0);
    LOG_INFO("[Havel] %s", msg ? msg : "");
    havel_pushnil(H);
    return HAVEL_OK;
}

// io.injectKey(code, pressed): script-driven input synthesis, the
// Wayland equivalent of the monorepo's X11 io.send. Feeds the key
// through the seat's current keyboard - the full compositor dispatch
// chain runs (keybindings, plugins), exactly like a real keypress.
// Code is the evdev keycode (the handler adds 8 for xkb).
extern "C" void havel_wlr_inject_key(struct havel_wlr_server*, uint32_t, bool);
static int hf_inject_key(HavelState* H, void*) {
    auto* ctx = (HavelCompositorCtx*)havel_getuserdata(H);
    int64_t code = arg_int(H, 0);
    int pressed = havel_gettop(H) >= 2 ? havel_toboolean(H, 1) : 1;
    if (ctx && ctx->server && code >= 0) {
        void* native = ctx->server->nativeHandle();
        if (native) {
            havel_wlr_inject_key((struct havel_wlr_server*)native,
                                  (uint32_t)code, pressed != 0);
        }
    }
    havel_pushnil(H);
    return HAVEL_OK;
}

static int hf_get_workspace(HavelState* H, void*) {
    auto* ctx = (HavelCompositorCtx*)havel_getuserdata(H);
    uint32_t ws = ctx && ctx->server ? ctx->server->windowManager().getCurrentWorkspace() : 0;
    havel_pushinteger(H, (int64_t)ws);
    return HAVEL_OK;
}

static int hf_get_windows(HavelState* H, void*) {
    auto* ctx = (HavelCompositorCtx*)havel_getuserdata(H);
    havel_pusharray(H);
    if (ctx && ctx->server) {
        auto windows = ctx->server->windowManager().getAllWindows();
        for (const auto& w : windows) {
            // Window object: {id, app_id, title, workspace}
            havel_pushobject(H);
            havel_pushinteger(H, (int64_t)w.id);
            havel_setfield(H, -2, "id");
            havel_pushstring(H, w.appId.c_str());
            havel_setfield(H, -2, "app_id");
            havel_pushstring(H, w.title.c_str());
            havel_setfield(H, -2, "title");
            havel_pushinteger(H, (int64_t)w.workspace);
            havel_setfield(H, -2, "workspace");
            havel_arrayappend(H, -2);
        }
    }
    return HAVEL_OK;
}

// ---------------------------------------------------------------------------
// keybind(spec, fn): Havel-defined keybindings
// ---------------------------------------------------------------------------

// Convert a compact havel hotkey spec ("^+F1", "#Return", "!" alone)
// into the canonical "Meta+Shift+..." form PluginConfig understands.
static std::string hotkeySpecToBinding(const std::string& hk) {
    std::string mods;
    std::string key;
    size_t i = 0;
    for (; i < hk.size(); ++i) {
        char c = hk[i];
        if (c == '^') mods += "Ctrl+";
        else if (c == '#') mods += "Meta+";
        else if (c == '!') mods += "Alt+";
        else if (c == '+' && i + 1 < hk.size()) mods += "Shift+";  // '+' followed by key
        else if (c == '+' && i + 1 >= hk.size()) { key = "+"; }       // "+" alone is the key
        else { key = hk.substr(i); break; }
    }
    if (key.empty()) key = hk;  // nothing consumed as modifiers
    if (key == "->") key = "Plus";  // only "+" would've consumed by typo; guard
    return mods + key;
}

static int hf_keybind(HavelState* H, void*) {
    auto* ctx = (HavelCompositorCtx*)havel_getuserdata(H);
    if (!ctx || !ctx->server) {
        havel_pushboolean(H, 0);
        return HAVEL_OK;
    }
    const char* rawSpec = arg_str(H, 0);
    int arg1type = havel_type(H, 1);
    // Any non-nil value qualifies: closures arrive as FUNCTION_OBJ_ID
    // values which don't satisfy the capi's TFUNCTION mapping over the
    // bridge. The runtime decides callable-ness at callback time.
    if (!rawSpec || !*rawSpec || arg1type == HAVEL_TNIL) {
        LOG_WARN("[Havel] keybind: needs (\"Mods+Key\", fn)");
        havel_pushboolean(H, 0);
        return HAVEL_OK;
    }

    // Translate havel hotkey syntax ("^+F1", "#Return", "!F1") into the
    // binding spec ("Ctrl+Shift+F1", "Meta+Return", "Alt+F1") so both the
    // host-function form keybind("Meta+Return", fn) and the language sugar
    // ^+F1 => {...} land in the same table.
    std::string spec = hotkeySpecToBinding(rawSpec);

    uint32_t mods = 0, keysym = 0;
    if (!PluginConfig::parseKeybindingSpec(spec.c_str(), &mods, &keysym)) {
        LOG_WARN("[Havel] keybind: unrecognized binding spec '%s' (translated: '%s')",
                 rawSpec, spec.c_str());
        havel_pushboolean(H, 0);
        return HAVEL_OK;
    }

    // Store the closure as a global so the KeybindingManager callback can
    // retrieve + call it later: pushvalue duplicates the arg, setglobal
    // stores the duplicate (and pops it).
    HavelHost* host = ctx->server->havelHost();
    std::string global = "havel_cb_" + std::to_string(++host->m_nextCallbackId);
    havel_pushvalue(H, 1);
    havel_setglobal(H, global.c_str());

    HavelState* HH = host->state();
    bool ok = ctx->server->registerHavelKeybinding(mods, keysym,
        ("havel:" + std::string(spec)).c_str(),
        [ctx, HH, global]() {
            if (!HH) return;
            havel_getglobal(HH, global.c_str());
            havel_call(HH, 0, 0);
        });

    havel_pushboolean(H, ok ? 1 : 0);
    return HAVEL_OK;
}

// ---------------------------------------------------------------------------
// HavelHost
// ---------------------------------------------------------------------------

HavelHost::HavelHost() = default;

HavelHost::~HavelHost() {
    if (m_H) {
        havel_close(m_H);
        m_H = nullptr;
    }
}

bool HavelHost::init(Server* server) {
    if (m_initialized) return true;
    m_server = server;

    m_H = havel_newstate();
    if (!m_H) {
        m_lastError = "havel_newstate failed";
        return false;
    }

    auto* ctx = new HavelCompositorCtx{server};
    havel_setuserdata(m_H, ctx);

    registerHostFunctions();
    m_initialized = true;
    LOG_INFO("[Havel] Language embedded (%s)", havel_version());
    return true;
}

void HavelHost::registerHostFunctions() {
    havel_pushcfunction_with_ctx(m_H, hf_spawn, nullptr, "spawn");
    havel_pop(m_H, 1);
    havel_pushcfunction_with_ctx(m_H, hf_workspace, nullptr, "workspace");
    havel_pop(m_H, 1);
    havel_pushcfunction_with_ctx(m_H, hf_workspace_next, nullptr, "workspace_next");
    havel_pop(m_H, 1);
    havel_pushcfunction_with_ctx(m_H, hf_workspace_prev, nullptr, "workspace_prev");
    havel_pop(m_H, 1);
    havel_pushcfunction_with_ctx(m_H, hf_focus, nullptr, "focus");
    havel_pop(m_H, 1);
    havel_pushcfunction_with_ctx(m_H, hf_close_window, nullptr, "close_window");
    havel_pop(m_H, 1);
    havel_pushcfunction_with_ctx(m_H, hf_minimize, nullptr, "minimize");
    havel_pop(m_H, 1);
    havel_pushcfunction_with_ctx(m_H, hf_maximize, nullptr, "maximize");
    havel_pop(m_H, 1);
    havel_pushcfunction_with_ctx(m_H, hf_restore, nullptr, "restore");
    havel_pop(m_H, 1);
    havel_pushcfunction_with_ctx(m_H, hf_move, nullptr, "move");
    havel_pop(m_H, 1);
    havel_pushcfunction_with_ctx(m_H, hf_resize, nullptr, "resize");
    havel_pop(m_H, 1);
    havel_pushcfunction_with_ctx(m_H, hf_set_floating, nullptr, "set_floating");
    havel_pop(m_H, 1);
    havel_pushcfunction_with_ctx(m_H, hf_set_gamma, nullptr, "set_gamma");
    havel_pop(m_H, 1);
    havel_pushcfunction_with_ctx(m_H, hf_set_temperature, nullptr, "set_temperature");
    havel_pop(m_H, 1);
    havel_pushcfunction_with_ctx(m_H, hf_set_brightness, nullptr, "set_brightness");
    havel_pop(m_H, 1);
    havel_pushcfunction_with_ctx(m_H, hf_notify, nullptr, "notify");
    havel_pop(m_H, 1);
    havel_pushcfunction_with_ctx(m_H, hf_log, nullptr, "log");
    havel_pop(m_H, 1);
    havel_pushcfunction_with_ctx(m_H, hf_get_workspace, nullptr, "get_workspace");
    havel_pop(m_H, 1);
    havel_pushcfunction_with_ctx(m_H, hf_get_windows, nullptr, "get_windows");
    havel_pop(m_H, 1);
    havel_pushcfunction_with_ctx(m_H, hf_inject_key, nullptr, "io.injectKey");
    havel_pop(m_H, 1);
    havel_pushcfunction_with_ctx(m_H, hf_keybind, nullptr, "keybind");
    havel_pop(m_H, 1);
    // Language-level hotkey syntax (^+F1 => {...}, #Return => {...})
    // lowers to calls to the global "hotkey.register". Register the same
    // handler under that dotted name; the keymap parse inside hf_keybind
    // normalizes the syntax sugar into the same mods+keysym form.
    havel_pushcfunction_with_ctx(m_H, hf_keybind, nullptr, "hotkey.register");
    havel_pop(m_H, 1);
}

bool HavelHost::runFile(const std::string& path) {
    if (!m_initialized || !m_H) {
        m_lastError = "not initialized";
        return false;
    }
    // C-API semantics: havel_loadfile already compiles AND executes
    // the chunk, leaving its return value on the stack. No havel_call
    // here - the stack top is a value, not a function.
    int status = havel_loadfile(m_H, path.c_str());
    if (status != HAVEL_OK) {
        m_lastError = havel_errmsg(m_H) ? havel_errmsg(m_H) : "load failed";
        LOG_ERROR("[Havel] Failed to load %s: %s", path.c_str(), m_lastError.c_str());
        havel_settop(m_H, 0);
        return false;
    }
    havel_pop(m_H, 1);
    LOG_INFO("[Havel] Executed %s", path.c_str());
    return true;
}

bool HavelHost::runString(const std::string& source, const std::string& name) {
    if (!m_initialized || !m_H) {
        m_lastError = "not initialized";
        return false;
    }
    // C-API semantics: havel_loadstring already compiles AND executes
    // the chunk, leaving its return value on the stack. No havel_call
    // here - the stack top is a value, not a function.
    int status = havel_loadstring(m_H, source.c_str(), name.c_str());
    if (status != HAVEL_OK) {
        m_lastError = havel_errmsg(m_H) ? havel_errmsg(m_H) : "load failed";
        LOG_ERROR("[Havel] Failed to compile %s: %s", name.c_str(), m_lastError.c_str());
        havel_settop(m_H, 0);
        return false;
    }
    havel_pop(m_H, 1);
    return true;
}
