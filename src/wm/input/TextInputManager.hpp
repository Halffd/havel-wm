#pragma once

#include <wayland-server-core.h>
#include <text-input-unstable-v3-protocol.h>
#include <string>
#include <vector>

struct wlr_surface;

namespace havel {

/**
 * Text Input Object
 * 
 * Represents a text input session between compositor and client.
 * Handles pre-edit and commit text communication.
 */
class TextInput {
public:
    TextInput(struct wl_client* client, uint32_t id, uint32_t version);
    ~TextInput();

    struct wl_resource* getResource() const { return m_resource; }
    bool isEnabled() const { return m_enabled; }

    // Send events to client
    void sendPreeditString(const std::string& text, int cursorBegin, int cursorEnd);
    void sendCommitString(const std::string& text);
    void sendDeleteSurroundingText(uint32_t beforeLength, uint32_t afterLength);
    void sendDone();

    // Handle requests from client
    void enable(uint32_t serial) { m_enabled = true; (void)serial; }
    void disable(uint32_t serial) { m_enabled = false; (void)serial; }

private:
    struct wl_client* m_client;
    struct wl_resource* m_resource = nullptr;
    uint32_t m_version;
    uint32_t m_serial = 0;
    bool m_enabled = false;
};

/**
 * Text Input Manager - Full IME support
 * 
 * Implements text-input-unstable-v3 protocol for Input Method Editor support.
 * Allows clients (like App Launcher) to receive composed text from IMEs.
 */
class TextInputManager {
public:
    TextInputManager(struct wl_display* display);
    ~TextInputManager();

    static TextInputManager* getInstance();

    // Enable/disable text input for a surface
    void enableTextInput(struct wl_surface* surface);
    void disableTextInput();

    // Set cursor position for pre-edit rendering
    void setCursorPosition(int x, int y);

    // Set pre-edit text (composed but not committed)
    void setPreeditText(const char* text);

    // Commit text (finalized input)
    void commitText(const char* text);

    // Delete surrounding text
    void deleteSurroundingText(uint32_t beforeLength, uint32_t afterLength);

    // Get current text state
    const char* getCommitText() const { return m_commitText; }
    const char* getPreeditText() const { return m_preeditText; }
    bool isActive() const { return m_active; }
    struct wl_surface* getCurrentSurface() const { return m_currentSurface; }

    // Create text input object (called from protocol bind)
    void createTextInput(struct wl_client* client, uint32_t version, uint32_t id);

private:
    static TextInputManager* s_instance;

    struct wl_global* m_global;
    std::vector<TextInput*> m_textInputs;
    struct wl_surface* m_currentSurface;

    char m_commitText[512];
    char m_preeditText[512];
    int m_cursorX;
    int m_cursorY;
    bool m_active;
};

} // namespace havel
