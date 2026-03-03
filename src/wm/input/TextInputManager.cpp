// Text Input Manager - IME support via text-input-unstable-v3
// Minimal implementation providing the protocol framework

#include "TextInputManager.hpp"
#include <text-input-unstable-v3-protocol.h>
#include <Logger.h>
#include <cstring>

namespace havel {

TextInputManager* TextInputManager::s_instance = nullptr;

// ============================================================================
// Text Input Object Implementation
// ============================================================================

TextInput::TextInput(struct wl_client* client, uint32_t id, uint32_t version)
    : m_client(client)
    , m_version(version)
    , m_enabled(false)
{
    m_resource = wl_resource_create(client, &zwp_text_input_v3_interface, version, id);
    if (!m_resource) {
        wl_client_post_no_memory(client);
        return;
    }
    
    LOG_DEBUG("[TextInput] Created text_input object for client %p", (void*)client);
}

TextInput::~TextInput() {
    if (m_resource) {
        wl_resource_destroy(m_resource);
    }
}

void TextInput::sendPreeditString(const std::string& text, int cursorBegin, int cursorEnd) {
    if (!m_resource) return;
    zwp_text_input_v3_send_preedit_string(m_resource, text.c_str(), cursorBegin, cursorEnd);
    m_serial++;
}

void TextInput::sendCommitString(const std::string& text) {
    if (!m_resource) return;
    zwp_text_input_v3_send_commit_string(m_resource, text.c_str());
    m_serial++;
}

void TextInput::sendDeleteSurroundingText(uint32_t beforeLength, uint32_t afterLength) {
    if (!m_resource) return;
    zwp_text_input_v3_send_delete_surrounding_text(m_resource, beforeLength, afterLength);
    m_serial++;
}

void TextInput::sendDone() {
    if (!m_resource) return;
    zwp_text_input_v3_send_done(m_resource, m_serial++);
}

// ============================================================================
// Text Input Manager Implementation
// ============================================================================

static void text_input_manager_bind(struct wl_client* client, void* data,
                                     uint32_t version, uint32_t id);

TextInputManager::TextInputManager(struct wl_display* display)
    : m_global(nullptr)
    , m_textInputs()
    , m_currentSurface(nullptr)
    , m_commitText{0}
    , m_preeditText{0}
    , m_cursorX(0)
    , m_cursorY(0)
    , m_active(false)
{
    m_global = wl_global_create(display, &zwp_text_input_manager_v3_interface, 1,
                                 this, text_input_manager_bind);
    if (!m_global) {
        LOG_ERROR("[TextInput] Failed to create text_input_manager_v3 global");
        return;
    }

    s_instance = this;
    LOG_INFO("[TextInput] text_input_manager_v3 initialized");
}

// Protocol bind implementation
static void text_input_manager_bind(struct wl_client* client, void* data,
                                     uint32_t version, uint32_t id) {
    auto* manager = static_cast<TextInputManager*>(data);
    manager->createTextInput(client, version, id);
}

TextInputManager::~TextInputManager() {
    if (m_global) {
        wl_global_destroy(m_global);
    }

    // Clean up all text input objects
    for (auto* textInput : m_textInputs) {
        delete textInput;
    }
    m_textInputs.clear();

    s_instance = nullptr;
}

TextInputManager* TextInputManager::getInstance() {
    return s_instance;
}

void TextInputManager::createTextInput(struct wl_client* client, uint32_t version, uint32_t id) {
    // Create new text input object
    auto* textInput = new TextInput(client, id, version);
    
    if (!textInput->getResource()) {
        delete textInput;
        wl_client_post_no_memory(client);
        return;
    }
    
    m_textInputs.push_back(textInput);
    
    LOG_INFO("[TextInput] Client %p created text_input (id=%u, version=%u)", (void*)client, id, version);
}

void TextInputManager::enableTextInput(struct wl_surface* surface) {
    if (!surface) return;

    m_active = true;
    m_commitText[0] = '\0';
    m_preeditText[0] = '\0';
    m_currentSurface = surface;

    LOG_INFO("[TextInput] Text input enabled for surface %p", (void*)surface);
}

void TextInputManager::disableTextInput() {
    m_active = false;
    m_commitText[0] = '\0';
    m_preeditText[0] = '\0';
    m_currentSurface = nullptr;

    LOG_INFO("[TextInput] Text input disabled");
}

void TextInputManager::setCursorPosition(int x, int y) {
    m_cursorX = x;
    m_cursorY = y;
}

void TextInputManager::setPreeditText(const char* text) {
    if (!text) text = "";
    
    strncpy(m_preeditText, text, sizeof(m_preeditText) - 1);
    m_preeditText[sizeof(m_preeditText) - 1] = '\0';
    
    // Send preedit to all enabled text inputs
    for (auto* textInput : m_textInputs) {
        if (textInput->isEnabled()) {
            textInput->sendPreeditString(std::string(m_preeditText), strlen(m_preeditText), strlen(m_preeditText));
            textInput->sendDone();
        }
    }
    
    LOG_DEBUG("[TextInput] Preedit: %s", m_preeditText);
}

void TextInputManager::commitText(const char* text) {
    if (!text) return;
    
    strncpy(m_commitText, text, sizeof(m_commitText) - 1);
    m_commitText[sizeof(m_commitText) - 1] = '\0';
    
    // Send commit to all enabled text inputs
    for (auto* textInput : m_textInputs) {
        if (textInput->isEnabled()) {
            textInput->sendCommitString(m_commitText);
            textInput->sendDone();
        }
    }
    
    // Clear preedit after commit
    m_preeditText[0] = '\0';
    
    LOG_INFO("[TextInput] Commit: %s", m_commitText);
}

void TextInputManager::deleteSurroundingText(uint32_t beforeLength, uint32_t afterLength) {
    // Send delete command to all enabled text inputs
    for (auto* textInput : m_textInputs) {
        if (textInput->isEnabled()) {
            textInput->sendDeleteSurroundingText(beforeLength, afterLength);
            textInput->sendDone();
        }
    }
}

} // namespace havel
