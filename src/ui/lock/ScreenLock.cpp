// Screen lock implementation. See ScreenLock.hpp for the architecture.

#include "ScreenLock.hpp"
#include <wm/render/OverlayRenderer.hpp>

#include <Logger.h>

#include <security/pam_appl.h>
#include <security/pam_misc.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <pwd.h>
#include <unistd.h>

namespace havel {

// ---------------------------------------------------------------------------
// PAM authentication
// ---------------------------------------------------------------------------

namespace {

// PAM conversation: supply the password when asked, deny other prompts.
struct PamData {
    const char* password;
};

int pamConverse(int numMsg, const struct pam_message** msg,
                struct pam_response** resp, void* appdata) {
    if (numMsg <= 0 || !msg || !resp) return PAM_CONV_ERR;
    auto* responses = (struct pam_response*)calloc(numMsg, sizeof(struct pam_response));
    if (!responses) return PAM_BUF_ERR;
    auto* pamData = (PamData*)appdata;

    for (int i = 0; i < numMsg; i++) {
        if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF) {
            // PAM wants the password
            responses[i].resp = pamData->password ? strdup(pamData->password) : strdup("");
            responses[i].resp_retcode = 0;
        } else {
            // PAM_INFO / PAM_ERROR prompts: respond with empty string
            responses[i].resp = strdup("");
            responses[i].resp_retcode = 0;
        }
    }
    *resp = responses;
    return PAM_SUCCESS;
}

const char* currentUserName() {
    struct passwd* pw = getpwuid(getuid());
    return pw ? pw->pw_name : "";
}

} // namespace

bool ScreenLock::authenticate() {
    const char* user = currentUserName();
    if (!user || !*user) {
        LOG_ERROR("[ScreenLock] cannot determine username for PAM");
        return false;
    }

    PamData pamData{m_password.c_str()};
    struct pam_conv conv = {pamConverse, &pamData};
    pam_handle_t* pamh = nullptr;

    // Use the "swaylock" PAM service: it ships on every distro that
    // packages swaylock (same unix_chkpwd helper, same config).
    int ret = pam_start("swaylock", user, &conv, &pamh);
    if (ret != PAM_SUCCESS) {
        LOG_ERROR("[ScreenLock] pam_start failed: %s", pam_strerror(pamh, ret));
        return false;
    }

    ret = pam_authenticate(pamh, 0);
    if (ret != PAM_SUCCESS) {
        LOG_INFO("[ScreenLock] PAM auth failed: %s", pam_strerror(pamh, ret));
        pam_end(pamh, ret);
        return false;
    }

    ret = pam_acct_mgmt(pamh, 0);
    pam_end(pamh, ret);
    if (ret != PAM_SUCCESS) {
        LOG_INFO("[ScreenLock] PAM acct_mgmt failed: %s", pam_strerror(pamh, ret));
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Lock / unlock / key handling
// ---------------------------------------------------------------------------

void ScreenLock::lock() {
    m_locked = true;
    m_password.clear();
    m_status = "Locked";
    m_dotCount = 0;
    LOG_INFO("[ScreenLock] locked");
}

void ScreenLock::unlock() {
    m_locked = false;
    m_password.clear();
    m_status.clear();
    m_dotCount = 0;
    LOG_INFO("[ScreenLock] unlocked");
}

bool ScreenLock::handleKey(uint32_t keysym, bool pressed, const std::string& utf8) {
    if (!m_locked || !pressed) return true;  // consume everything while locked

    // Backspace: delete last character
    if (keysym == 0xFF08 /* BackSpace */) {
        if (!m_password.empty()) m_password.pop_back();
        return true;
    }

    // Return / Enter: submit for authentication
    if (keysym == 0xFF0D /* Return */ || keysym == 0xFF8D /* KP_Enter */) {
        if (m_password.empty()) return true;  // nothing to auth
        if (authenticate()) {
            unlock();
        } else {
            m_failedAttempts++;
            m_status = "Wrong password";
            m_password.clear();
        }
        return true;
    }

    // Escape: clear the password buffer (not unlock)
    if (keysym == 0xFF1B /* Escape */) {
        m_password.clear();
        m_status.clear();
        return true;
    }

    // Printable characters (from UTF-8 or keysym): append to buffer
    if (!utf8.empty() && (unsigned char)utf8[0] >= 0x20 && (unsigned char)utf8[0] < 0x7F) {
        m_password += utf8;
    } else if (keysym >= 0x20 && keysym < 0x7F) {
        // keysym for ASCII maps to the char
        m_password += (char)keysym;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void ScreenLock::render(OverlayRenderer* renderer, int ox, int oy, int w, int h) {
    if (!renderer || !m_locked) return;

    // Full-screen dark background
    renderer->drawRect((float)ox, (float)oy, (float)w, (float)h,
                       Color(0.02f, 0.02f, 0.04f, 0.98f));

    // Clock (large, centered in the upper third)
    time_t now = time(nullptr);
    struct tm* tm = localtime(&now);
    char timeStr[16], dateStr[64];
    strftime(timeStr, sizeof timeStr, "%H:%M", tm);
    strftime(dateStr, sizeof dateStr, "%A, %B %d", tm);

    renderer->drawTextCentered(timeStr,
                               (float)(ox + w / 2), (float)(oy + h / 4),
                               72.0f, Color(0.95f, 0.95f, 0.95f, 1.0f));
    renderer->drawTextCentered(dateStr,
                               (float)(ox + w / 2), (float)(oy + h / 4 + 60),
                               20.0f, Color(0.6f, 0.6f, 0.65f, 1.0f));

    // Password dots (one per character)
    if (!m_password.empty()) {
        std::string dots(m_password.size(), '*');
        renderer->drawTextCentered(dots.c_str(),
                                   (float)(ox + w / 2), (float)(oy + h / 2 + 40),
                                   24.0f, Color(0.7f, 0.7f, 0.75f, 1.0f));
    }

    // Status message
    if (!m_status.empty()) {
        renderer->drawTextCentered(m_status.c_str(),
                                   (float)(ox + w / 2), (float)(oy + h / 2 + 80),
                                   16.0f,
                                   m_status == "Wrong password"
                                       ? Color(0.9f, 0.3f, 0.3f, 1.0f)
                                       : Color(0.5f, 0.5f, 0.55f, 1.0f));
    }

    // Bottom hint
    renderer->drawTextCentered("Enter password to unlock",
                               (float)(ox + w / 2), (float)(oy + h - 60),
                               14.0f, Color(0.4f, 0.4f, 0.45f, 1.0f));
}

} // namespace havel
