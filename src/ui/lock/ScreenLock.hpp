#pragma once

// Screen lock for havel-wm.
//
// A compositor-internal lock screen (not a plugin): when locked, ALL
// keyboard input is intercepted before keybindings and plugins see it,
// the pointer is prevented from reaching clients, and a full-screen
// overlay renders on every output.
//
// Authentication uses PAM (the "swaylock" service, which ships on every
// distro that ships swaylock — same config, same unix_chkpwd helper).
//
// Wayland integration: rendered through the OverlayRenderer (font module
// for text, scene buffers for pixels) — the same path every other
// overlay uses. Each output gets its own pass with its own box.

#include <cstdint>
#include <string>

namespace havel {

class OverlayRenderer;

class ScreenLock {
public:
    ScreenLock() = default;
    ~ScreenLock() = default;

    // Enter the locked state. Clears the password buffer, resets the
    // auth status. All subsequent key events are consumed.
    void lock();

    // Exit the locked state. Called internally after successful
    // authentication, or explicitly (IPC unlock_screen for testing).
    void unlock();

    bool isLocked() const { return m_locked; }

    // Handle a key event while locked. Returns true (always — the lock
    // consumes everything). Call from Server::handleKey BEFORE keybinding
    // and plugin dispatch.
    bool handleKey(uint32_t keysym, bool pressed, const std::string& utf8);

    // Render the lock overlay for one output pass. Called from the
    // overlay render loop when isLocked().
    void render(class OverlayRenderer* renderer, int ox, int oy, int w, int h);

    // Number of failed auth attempts this session.
    int failedAttempts() const { return m_failedAttempts; }

private:
    // Attempt PAM authentication with the current password buffer.
    bool authenticate();

    bool m_locked = false;
    std::string m_password;    // current typed password (not displayed)
    std::string m_status;      // "Wrong password" / "Locked" / ...
    int m_failedAttempts = 0;
    int m_dotCount = 0;        // rendered dots (managed for smooth updates)
};

} // namespace havel
