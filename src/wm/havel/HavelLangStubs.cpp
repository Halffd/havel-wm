// Leaf stubs for havel-lang archive links.
//
// The 2026-09 monorepo commits made libhavel_core qt-free (clipboard
// behind a backend factory, Qt UI backend moved out of core), so the
// earlier QClipboard/GUIManager stubs are dead - the archives are
// Qt-free now. Still referenced (and still stubbed here):
// - ExecutionEngine::executeFrame: declared in the archive, never
//   defined (the compositor drives frames itself; scripts never pump)
// - PipeWire symbols: the media backend's audio paths degrade to no-ops
// - qt_version_tag: data symbol referenced by objects compiled with
//   Qt headers

namespace havel { namespace compiler {

class ExecutionEngine {
public:
    void executeFrame();
};

// The VM's frame-pump hook: the compositor drives frames itself
// (output_frame -> wlr_scene_output_commit); scripts never pump.
void ExecutionEngine::executeFrame() {}

}} // namespace havel::compiler

// Qt version tag (archive objects carry a .qtversion section referencing
// this data symbol; Qt itself is not linked)
extern "C" {
int qt_version_tag = 0;
}

// PipeWire symbols (AudioBackendImpl.cpp). PipeWire is not a build
// dependency of the WM.
#include <cstddef>
extern "C" {
void pw_init(int*, char***) { }
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
