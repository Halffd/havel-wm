// Tests for startup renderer selection.
//
// What is tested: the user-facing renderer names map to the canonical
// wlroots WLR_RENDERER values. OpenGL aliases must select OpenGL ES 2
// ("gles2"), never the unsupported literal value "opengl".
// Expected result: every assertion passes and the process exits with 0.

#include <wm/RendererSelection.hpp>

#include <cstdio>
#include <string>

using havel::RendererBackend;

static int failed = 0;

static void check(bool condition, const char* message) {
    if (!condition) {
        printf("FAILED: %s\n", message);
        failed++;
    }
}

static bool parses(const char* name, RendererBackend expected) {
    RendererBackend backend = RendererBackend::Auto;
    return havel::parseRendererBackend(name, &backend) && backend == expected;
}

int main() {
    check(parses("auto", RendererBackend::Auto),
          "auto should select automatic renderer detection");
    check(parses("wlroots", RendererBackend::Auto),
          "wlroots should be an alias for automatic renderer detection");
    check(parses("opengl", RendererBackend::OpenGL),
          "opengl should select the OpenGL ES 2 renderer");
    check(parses("opengles", RendererBackend::OpenGL),
          "opengles should select the OpenGL ES 2 renderer");
    check(parses("gles", RendererBackend::OpenGL),
          "gles should select the OpenGL ES 2 renderer");
    check(parses("gles2", RendererBackend::OpenGL),
          "gles2 should select the OpenGL ES 2 renderer");
    check(parses("vulkan", RendererBackend::Vulkan),
          "vulkan should select the Vulkan renderer");

    check(havel::rendererEnvironmentValue(RendererBackend::Auto) == nullptr,
          "automatic selection must not override an inherited WLR_RENDERER");
    check(std::string(havel::rendererEnvironmentValue(RendererBackend::OpenGL)) == "gles2",
          "OpenGL selection must use wlroots' canonical gles2 value");
    check(std::string(havel::rendererEnvironmentValue(RendererBackend::Vulkan)) == "vulkan",
          "Vulkan selection must use wlroots' canonical vulkan value");

    RendererBackend backend = RendererBackend::Auto;
    check(!havel::parseRendererBackend("bogus", &backend),
          "unknown renderer names must be rejected");
    check(!havel::parseRendererBackend("", &backend),
          "empty renderer names must be rejected");
    check(!havel::parseRendererBackend("opengl3", &backend),
          "renderer names must match exactly");

    if (failed == 0) {
        printf("All renderer selection tests passed!\n");
        return 0;
    }
    printf("%d renderer selection tests FAILED\n", failed);
    return 1;
}
