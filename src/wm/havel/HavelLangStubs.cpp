// Leaf stubs for havel-lang host bridges that have no backend in the
// Wayland compositor context.
//
// The havel-lang archive's bridge layer (ModularHostBridges) drives the
// monorepo's X11-based app services (Qt clipboard, GUI notifications,
// the ExecutionEngine frame pump). The WM has its own implementations
// (NotificationDaemon, IPC, frame loop) and does not link Qt. These
// symbols must exist for the link; they log-and-noop - honest
// degradation for features the WM covers by other means.
//
// NOTE: definitions are OUT-OF-LINE on purpose - in-class definitions
// are implicitly inline and are never emitted when unreferenced in
// their own TU, which silently defeated the first stub attempt.

#include <string>

// Qt's QClipboard: minimal stub with matching name mangling (the
// archive's references mangle it unnamespaced: _ZN10QClipboard...).
// Qt itself is not linked.
class QClipboard {
public:
    enum Mode { Clipboard, Selection, SelectionBuffer, FindBuffer };
    void clear(Mode mode);
};

namespace havel {

class ClipboardManager {
public:
    // The real signature returns QClipboard* (pointer - no cxx11 ABI tag,
    // which is why a std::string-returning stub mangled differently and
    // never matched the archive's reference)
    QClipboard* getClipboard() const;
};

// Out-of-line: always emitted, satisfies the archive's reference.
QClipboard* ClipboardManager::getClipboard() const {
    // WM clipboard access goes through wlr-data-control / IPC,
    // not the monorepo's Qt backend
    return nullptr;
}

class GUIManager {
public:
    void showNotification(const std::string& summary, const std::string& body,
                          const std::string& app, int timeout);
};

void GUIManager::showNotification(const std::string& summary, const std::string& body,
                                  const std::string& app, int timeout) {
    // The WM routes notifications through its D-Bus daemon
    // (NotificationDaemon); this bridge path is unused here.
    (void)summary; (void)body; (void)app; (void)timeout;
}

} // namespace havel

namespace havel { namespace compiler {

class ExecutionEngine {
public:
    void executeFrame();
};

// The VM's frame-pump hook: the compositor drives frames itself
// (output_frame -> wlr_scene_output_commit); scripts never pump.
void ExecutionEngine::executeFrame() {
}

}} // namespace havel::compiler

// Qt's QCoreApplication::exit(int) - the only other Qt symbol the
// bridge layer needs. A minimal stub with matching name mangling; Qt
// itself is not linked (there is no Qt event loop in the compositor).
class QCoreApplication {
public:
    static void exit(int exitCode);
};

void QCoreApplication::exit(int exitCode) {
    (void)exitCode;
}

// Qt version tag (EventListener.cpp compiled with Qt headers carries a
// .qtversion section referencing this data symbol)
extern "C" {
int qt_version_tag = 0;
}

void QClipboard::clear(QClipboard::Mode mode) {
    (void)mode;  // no Qt event loop in the compositor
}

// PipeWire symbols (AudioBackendImpl.cpp). PipeWire is not a build
// dependency of the WM; the media bridge's audio paths degrade to
// no-ops (the WM has no audio backend of its own either).
extern "C" {
void pw_init(int*, char***) { (void)0; }
struct pw_thread_loop;
struct pw_context;
struct pw_core;
struct pw_loop;

struct pw_thread_loop* pw_thread_loop_new(const char*, void*) { return nullptr; }
void pw_thread_loop_destroy(struct pw_thread_loop*) {}
struct pw_loop* pw_thread_loop_get_loop(struct pw_thread_loop*) { return nullptr; }
void pw_thread_loop_lock(struct pw_thread_loop*) {}
void pw_thread_loop_unlock(struct pw_thread_loop*) {}
void pw_thread_loop_signal(struct pw_thread_loop*, int) {}
void pw_thread_loop_wait(struct pw_thread_loop*) {}
int pw_thread_loop_start(struct pw_thread_loop*) { return -1; }
void pw_thread_loop_stop(struct pw_thread_loop*) {}

struct pw_context* pw_context_new(struct pw_loop*, void*, size_t, void*) { return nullptr; }
void pw_context_destroy(struct pw_context*) {}
struct pw_core* pw_context_connect(struct pw_context*, void*, size_t) { return nullptr; }
int pw_core_disconnect(struct pw_core*) { return -1; }
}
