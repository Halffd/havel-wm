// Screen lock implementation. See ScreenLock.hpp for the architecture.

#include "ScreenLock.hpp"
#include <wm/render/OverlayRenderer.hpp>

#include <Logger.h>

#include <security/pam_appl.h>
#include <security/pam_misc.h>

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <csignal>
#include <ctime>
#include <pwd.h>
#include <unistd.h>
#include <sys/wait.h>

namespace havel {

// ---------------------------------------------------------------------------
// PAM authentication
// ---------------------------------------------------------------------------

namespace {

// PAM conversation: supply the password when asked, deny other prompts.
struct PamData {
    const char* password;
    // Filled in by pamConverse so the caller can zero the password
    // copies PAM holds after pam_authenticate (before pam_end frees).
    struct pam_response* responses = nullptr;
    int responseCount = 0;
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

    // Remember the buffers so authenticate() can scrub them after use.
    pamData->responses = responses;
    pamData->responseCount = numMsg;
    return PAM_SUCCESS;
}

// Zero every response buffer the conversation filled (they hold strdup'd
// copies of the password). Must run before pam_end frees them.
void scrubPamResponses(PamData& pd) {
    if (!pd.responses) return;
    for (int i = 0; i < pd.responseCount; i++) {
        char* r = pd.responses[i].resp;
        if (!r) continue;
        size_t len = strlen(r);
        volatile char* p = (volatile char*)r;
        for (size_t n = 0; n < len; n++) p[n] = 0;
    }
}

// Try the PAM service chain. pam_start's second argument is the service
// name (the file looked up under /etc/pam.d/): use "swaylock" (the
// canonical screen-locker service) and fall back to "login", which every
// PAM-using distro ships, for systems without /etc/pam.d/swaylock.
pam_handle_t* startPamChain(const char* user, const struct pam_conv* conv) {
    for (const char* svc : {"swaylock", "login"}) {
        pam_handle_t* pamh = nullptr;
        int ret = pam_start(svc, user, conv, &pamh);
        if (ret == PAM_SUCCESS) return pamh;
        LOG_INFO("[ScreenLock] pam_start(%s) failed: %s", svc,
                 pamh ? pam_strerror(pamh, ret) : "no handle");
        if (pamh) pam_end(pamh, ret);
    }
    return nullptr;
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
        scrubPassword();
        return false;
    }

    PamData pamData;
    pamData.password = m_password.c_str();
    struct pam_conv conv = {pamConverse, &pamData};


    pam_handle_t* pamh = startPamChain(user, &conv);
    if (!pamh) {
        // Design choice: with NO usable PAM service at all, every auth
        // attempt would fail and the session would be bricked behind a
        // lock that can never be satisfied. Refusing to keep the user
        // locked is strictly safer than a permanent lockout, so unlock
        // and log loudly. (lock() probes the same chain up front and
        // refuses to engage, so this only fires if PAM breaks while
        // already locked.)
        LOG_ERROR("[ScreenLock] no usable PAM service (tried swaylock, "
                   "login) — unlocking to avoid a bricked session");
        scrubPassword();
        unlock();
        return false;
    }

    int ret = pam_authenticate(pamh, 0);
    // The conversation copied the password into PAM's response buffers;
    // zero them now that pam_authenticate is done (pam_end frees them).
    scrubPamResponses(pamData);
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

// Zero the typed-password buffer so the material does not linger in freed
// heap memory. Volatile store so the compiler cannot elide the scrub.
void ScreenLock::scrubPassword() {
    if (!m_password.empty()) {
        volatile char* p = m_password.data();
        for (size_t i = 0; i < m_password.size(); i++) p[i] = 0;
    }
    m_password.clear();
}

// ---------------------------------------------------------------------------
// Async authentication (fork + pipe, polled from the render loop)
// ---------------------------------------------------------------------------
// A wrong password costs real time: PAM modules delay and unix_chkpwd
// forks. Running pam_authenticate inline froze the whole compositor for
// seconds per attempt (no frames, no input) — observed live. The worker
// child runs the full PAM chain and writes one verdict byte to a pipe;
// pollAuth() (called from render, ~60 Hz) collects it without blocking.

void ScreenLock::startAuth() {
    if (m_authBusy) return;

    if (pipe(m_authPipe) != 0) {
        LOG_ERROR("[ScreenLock] pipe() failed, falling back to sync auth");
        m_authPipe[0] = m_authPipe[1] = -1;
        if (authenticate()) {
            unlock();
        } else if (m_locked) {
            m_failedAttempts++;
            m_status = "Wrong password";
            scrubPassword();
        }
        return;
    }

    // Copy the password for the child BEFORE fork: the child reads it
    // from this same buffer, and the parent scrubs its copy right after
    // fork so the material lives in exactly one process at a time.
    std::string childPassword = m_password;

    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERROR("[ScreenLock] fork() failed, falling back to sync auth");
        close(m_authPipe[0]); close(m_authPipe[1]);
        m_authPipe[0] = m_authPipe[1] = -1;
        if (authenticate()) {
            unlock();
        } else if (m_locked) {
            m_failedAttempts++;
            m_status = "Wrong password";
            scrubPassword();
        }
        return;
    }

    if (pid == 0) {
        // ---- child: run PAM, write verdict, _exit ----
        close(m_authPipe[0]);
        // Restore default dispositions the compositor may have changed;
        // PAM helpers (unix_chkpwd) and signals must behave normally.
        signal(SIGPIPE, SIG_DFL);
        bool ok = false;
        {
            // Swap the buffer so authenticate() sees the copied password.
            std::string saved;
            saved.swap(m_password);
            m_password = childPassword;
            // scrubPassword() in the child's failure paths only touches
            // the child's copy — exactly what we want.
            ok = authenticate();
            // authenticate() scrubbed m_password on every exit path.
        }
        char verdict = ok ? 1 : 0;
        ssize_t w = write(m_authPipe[1], &verdict, 1);
        (void)w;
        close(m_authPipe[1]);
        _exit(ok ? 0 : 1);
    }

    // ---- parent ----
    close(m_authPipe[1]);
    m_authPipe[1] = -1;
    scrubPassword();          // parent copy gone; child owns it now
    m_authBusy = true;
    m_status = "Checking...";
    LOG_INFO("[ScreenLock] auth worker forked (pid %d)", (int)pid);
}

void ScreenLock::pollAuth() {
    if (!m_authBusy || m_authPipe[0] < 0) return;

    char verdict = -1;
    ssize_t n = read(m_authPipe[0], &verdict, 1);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;  // still running
        LOG_ERROR("[ScreenLock] auth pipe read error");
        verdict = 0;
    } else if (n == 0) {
        // EOF without a verdict byte: child crashed. Treat as failure
        // but distinguish it from a wrong password.
        LOG_ERROR("[ScreenLock] auth worker died without a verdict");
        verdict = 0;
        m_status = "auth worker crashed";
    }

    // Verdict landed: reap the child and close the pipe.
    int wstatus = 0;
    while (waitpid(-1, &wstatus, WNOHANG) > 0) {}
    close(m_authPipe[0]);
    m_authPipe[0] = -1;
    m_authBusy = false;

    if (verdict == 1) {
        unlock();
    } else if (m_locked) {
        m_failedAttempts++;
        if (m_status != "auth worker crashed") m_status = "Wrong password";
        LOG_INFO("[ScreenLock] auth failed (async)");
    }
}


// ---------------------------------------------------------------------------
// Lock / unlock / key handling
// ---------------------------------------------------------------------------

void ScreenLock::lock() {
    // Probe the PAM chain before engaging: if no service can start, do
    // NOT lock (there would be no way back in). lock() only returns
    // with isLocked() == true when authentication is actually possible.
    // (The idle auto-lock retries every frame, so log the refusal once.)
    static bool refusalLogged = false;
    struct pam_conv conv = {pamConverse, nullptr};
    pam_handle_t* probe = startPamChain(currentUserName(), &conv);
    if (!probe) {
        if (!refusalLogged) {
            LOG_ERROR("[ScreenLock] refusing to lock: no usable PAM service "
                       "(tried swaylock, login)");
            refusalLogged = true;
        }
        m_status = "authentication backend unavailable";
        return;
    }
    pam_end(probe, PAM_SUCCESS);

    m_locked = true;
    scrubPassword();
    m_status = "Locked";
    m_dotCount = 0;
    LOG_INFO("[ScreenLock] locked");
}

void ScreenLock::unlock() {
    m_locked = false;
    scrubPassword();
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
        if (m_authBusy) return true;          // a check is already running
        startAuth();
        return true;
    }

    // Escape: clear the password buffer (not unlock)
    if (keysym == 0xFF1B /* Escape */) {
        scrubPassword();
        m_status.clear();
        return true;
    }

    // Printable characters (from UTF-8 or keysym): append to the buffer.
    // UTF-8: accept any non-empty string whose first byte is not a C0
    // control char (< 0x20) and not DEL (0x7F). Multi-byte sequences
    // (é, 安, …) have a lead byte >= 0xC2, so the old ASCII-only check
    // silently dropped them and non-ASCII passwords were untypeable —
    // a permanent lockout. ASCII still also works via the keysym path.
    if (!utf8.empty() && (unsigned char)utf8[0] >= 0x20 &&
        (unsigned char)utf8[0] != 0x7F) {
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

    // Collect the auth verdict from a running worker (no-op when idle).
    pollAuth();

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
