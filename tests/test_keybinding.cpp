// Tests for PluginConfig::parseKeybindingSpec — the config keybinding
// parser used by plugin "keybinding" entries.
//
// What is tested: modifier parsing (Meta/Ctrl/Alt/Shift, aliases),
// keysym resolution via xkbcommon (case-insensitive), rejection of
// unknown tokens and empty specs, and exact expected bitsym values.

#include <wm/plugins/PluginConfig.hpp>
#include <cstdio>
#include <cstdint>

using havel::PluginConfig;

static int failed = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        printf("FAILED: %s\n", msg);
        failed++;
    }
}

int main() {
    // XKB modifier bits (xkbcommon): Shift=0, CapsLock=1, Ctrl=2, Alt=3,
    // Meta/Mod4=6
    const uint32_t SHIFT = 1u << 0;
    const uint32_t CTRL = 1u << 2;
    const uint32_t ALT = 1u << 3;
    const uint32_t META = 1u << 6;

    uint32_t mods = 0, sym = 0;

    // 1. Simple meta binding
    check(PluginConfig::parseKeybindingSpec("Meta+D", &mods, &sym),
          "Meta+D should parse");
    check(mods == META, "Meta+D mods should be Meta only");
    check(sym == XKB_KEY_d, "Meta+D keysym should be XKB_KEY_d");

    // 2. Meta+Shift with lowercase keysym
    check(PluginConfig::parseKeybindingSpec("Meta+Shift+w", &mods, &sym),
          "Meta+Shift+w should parse");
    check(mods == (META | SHIFT), "Meta+Shift+w mods should be Meta|Shift");
    check(sym == XKB_KEY_w, "Meta+Shift+w keysym should be XKB_KEY_w");

    // 3. Uppercase key letter (case-insensitive resolution)
    check(PluginConfig::parseKeybindingSpec("Meta+W", &mods, &sym),
          "Meta+W should parse");
    check(sym == XKB_KEY_w, "Meta+W keysym should resolve to lowercase w");

    // 4. Ctrl+Meta combo
    check(PluginConfig::parseKeybindingSpec("Ctrl+Meta+F4", &mods, &sym),
          "Ctrl+Meta+F4 should parse");
    check(mods == (CTRL | META), "Ctrl+Meta+F4 mods should be Ctrl|Meta");
    check(sym == XKB_KEY_F4, "Ctrl+Meta+F4 keysym should be F4");

    // 5. Alt+Tab
    check(PluginConfig::parseKeybindingSpec("Alt+Tab", &mods, &sym),
          "Alt+Tab should parse");
    check(mods == ALT, "Alt+Tab mods should be Alt only");
    check(sym == XKB_KEY_Tab, "Alt+Tab keysym should be Tab");

    // 6. Aliases: Control, Super, Logo
    check(PluginConfig::parseKeybindingSpec("Control+Super+L", &mods, &sym),
          "Control+Super+L should parse (aliases)");
    check(mods == (CTRL | META), "Control+Super+L mods via aliases");
    check(sym == XKB_KEY_l, "Control+Super+L keysym should be l");

    // 7. Named function keys
    check(PluginConfig::parseKeybindingSpec("Meta+Return", &mods, &sym),
          "Meta+Return should parse");
    check(sym == XKB_KEY_Return, "Meta+Return keysym should be Return");

    check(PluginConfig::parseKeybindingSpec("Meta+Escape", &mods, &sym),
          "Meta+Escape should parse");
    check(sym == XKB_KEY_Escape, "Meta+Escape keysym should be Escape");

    check(PluginConfig::parseKeybindingSpec("Alt+space", &mods, &sym),
          "Alt+space should parse");
    check(sym == XKB_KEY_space, "Alt+space keysym should be space");

    // 8. Rejections
    check(!PluginConfig::parseKeybindingSpec("", &mods, &sym),
          "empty spec should fail");
    check(!PluginConfig::parseKeybindingSpec("Meta", &mods, &sym),
          "modifier-only spec should fail");
    check(!PluginConfig::parseKeybindingSpec("Meta+NotAKey", &mods, &sym),
          "unknown keysym should fail");
    check(!PluginConfig::parseKeybindingSpec("Meta+Banana+X", &mods, &sym),
          "unknown modifier should fail");
    check(!PluginConfig::parseKeybindingSpec("W+X", &mods, &sym),
          "two keysyms should fail");

    if (failed == 0) {
        printf("All keybinding parser tests passed!\n");
        return 0;
    }
    printf("%d keybinding parser tests FAILED\n", failed);
    return 1;
}
