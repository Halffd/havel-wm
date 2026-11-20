// Tests for shifted-letter keybinding matching.
//
// What is tested: xkb reports the shifted symbol for a letter (P becomes
// 0x50), while bindings are normally registered with the base letter
// (0x70). A Meta+Shift+P binding must match the shifted keysym when the
// Shift modifier is active.
//
// Expected result: every assertion passes and the process exits with 0.

#include <input/KeybindingManager.hpp>

#include <xkbcommon/xkbcommon-keysyms.h>

#include <cstdio>

using havel::KeybindingManager;

static int failed = 0;

static void check(bool condition, const char* message) {
    if (!condition) {
        printf("FAILED: %s\n", message);
        failed++;
    }
}

int main() {
    KeybindingManager manager;
    bool fired = false;
    const uint32_t modifiers = KeybindingManager::MOD_LOGO | KeybindingManager::MOD_SHIFT;

    check(manager.registerKeybinding(modifiers, XKB_KEY_p, "toggle_compositing",
                                    [&fired]() { fired = true; }),
          "Meta+Shift+P should register");

    check(manager.handleKey(XKB_KEY_P, true, modifiers),
          "shifted P keysym should trigger a base-letter binding");
    check(fired, "the matching callback should run exactly once");
    check(!manager.handleKey(XKB_KEY_P, false, modifiers),
          "key releases should not trigger a binding");
    check(!manager.handleKey(XKB_KEY_P, true, KeybindingManager::MOD_LOGO),
          "shifted P without Shift must not trigger the binding");

    if (failed == 0) {
        printf("All keybinding manager tests passed!\n");
        return 0;
    }
    printf("%d keybinding manager tests FAILED\n", failed);
    return 1;
}
