#pragma once

namespace havel {

// Renderer choices exposed by Havel's startup command line. OpenGL maps to
// wlroots' OpenGL ES 2 renderer ("gles2"), which is the renderer backend
// supported by wlroots 0.20 for accelerated window rendering.
enum class RendererBackend {
    Auto,
    OpenGL,
    Vulkan,
};

// Parse a user-facing renderer name. Returns false for unknown names.
bool parseRendererBackend(const char* name, RendererBackend* backend);

// Canonical WLR_RENDERER value for a backend. nullptr means automatic
// selection and no environment override.
const char* rendererEnvironmentValue(RendererBackend backend);

} // namespace havel
