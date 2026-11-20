#include <wm/RendererSelection.hpp>

#include <cstring>

namespace havel {

bool parseRendererBackend(const char* name, RendererBackend* backend) {
    if (!name || !backend) {
        return false;
    }

    if (std::strcmp(name, "auto") == 0 ||
        std::strcmp(name, "wlroots") == 0) {
        *backend = RendererBackend::Auto;
        return true;
    }

    if (std::strcmp(name, "opengl") == 0 ||
        std::strcmp(name, "opengles") == 0 ||
        std::strcmp(name, "gles") == 0 ||
        std::strcmp(name, "gles2") == 0) {
        *backend = RendererBackend::OpenGL;
        return true;
    }

    if (std::strcmp(name, "vulkan") == 0) {
        *backend = RendererBackend::Vulkan;
        return true;
    }

    return false;
}

const char* rendererEnvironmentValue(RendererBackend backend) {
    switch (backend) {
        case RendererBackend::Auto:
            return nullptr;
        case RendererBackend::OpenGL:
            return "gles2";
        case RendererBackend::Vulkan:
            return "vulkan";
    }
    return nullptr;
}

} // namespace havel
