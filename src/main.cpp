#include <wm/wlr_bridge.h>
#include <core/LoadingScreen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef enum {
    RENDERER_AUTO,
    RENDERER_GLES2,
    RENDERER_VULKAN,
    RENDERER_WLROOTS
} RendererBackend;

static void print_usage(const char* prog) {
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("\nOptions:\n");
    printf("  -s, --startup <command>  Command to execute on startup\n");
    printf("  --no-loading-screen      Disable loading screen\n");
    printf("  -r, --renderer <backend> Select renderer backend:\n");
    printf("                             auto     - Auto-detect (default)\n");
    printf("                             gles     - GLES2 software rendering\n");
    printf("                             vulkan   - Vulkan + GLES2 hybrid\n");
    printf("                             wlroots  - Pure wlroots scene graph\n");
    printf("  -h, --help               Show this help message\n");
    printf("\nExamples:\n");
    printf("  %s -s 'foot'             Start foot terminal on launch\n", prog);
    printf("  %s -r vulkan -s 'swaybg' Use Vulkan renderer with wallpaper\n", prog);
    printf("  %s -r gles               Force GLES2 (no Vulkan)\n", prog);
    printf("  %s --no-loading-screen   Start without loading screen\n", prog);
}

int main(int argc, char* argv[]) {
    const char* startup_cmd = NULL;
    bool loading_screen_enabled = true;
    RendererBackend renderer = RENDERER_AUTO;

    // Parse command-line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--startup") == 0) {
            if (i + 1 < argc) {
                startup_cmd = argv[++i];
            } else {
                fprintf(stderr, "Error: -s requires a command argument\n");
                print_usage(argv[0]);
                return 1;
            }
        } else if (strcmp(argv[i], "--no-loading-screen") == 0) {
            loading_screen_enabled = false;
        } else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--renderer") == 0) {
            if (i + 1 < argc) {
                i++;
                if (strcmp(argv[i], "auto") == 0) {
                    renderer = RENDERER_AUTO;
                } else if (strcmp(argv[i], "gles") == 0 || strcmp(argv[i], "gles2") == 0) {
                    renderer = RENDERER_GLES2;
                } else if (strcmp(argv[i], "vulkan") == 0) {
                    renderer = RENDERER_VULKAN;
                } else if (strcmp(argv[i], "wlroots") == 0) {
                    renderer = RENDERER_WLROOTS;
                } else {
                    fprintf(stderr, "Error: Unknown renderer '%s'\n", argv[i]);
                    print_usage(argv[0]);
                    return 1;
                }
            } else {
                fprintf(stderr, "Error: -r requires a renderer argument\n");
                print_usage(argv[0]);
                return 1;
            }
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    havel_wlr_server_t* server = havel_wlr_create();
    if (!server) {
        return 1;
    }

    // Configure renderer based on command-line option
    switch (renderer) {
        case RENDERER_AUTO:
            printf("[MAIN] Renderer: Auto-detect (Vulkan + GLES2 fallback)\n");
            break;
        case RENDERER_GLES2:
            printf("[MAIN] Renderer: GLES2 (software rendering)\n");
            // GLES2 is initialized on-demand in VulkanRenderer
            break;
        case RENDERER_VULKAN:
            printf("[MAIN] Renderer: Vulkan + GLES2 hybrid\n");
            // Enable Vulkan renderer mode
            havel_wlr_set_renderer_mode(server, 1);
            break;
        case RENDERER_WLROOTS:
            printf("[MAIN] Renderer: Pure wlroots scene graph\n");
            // wlroots scene graph is default, no special config needed
            break;
    }

    // Disable loading screen if requested
    if (!loading_screen_enabled) {
        struct LoadingScreenConfig* config = loading_screen_get_config();
        config->enabled = false;
    }

    // Execute startup command if specified
    if (startup_cmd) {
        printf("[MAIN] Executing startup command: %s\n", startup_cmd);
        pid_t pid = fork();
        if (pid == 0) {
            // Child process - execute command
            execl("/bin/sh", "/bin/sh", "-c", startup_cmd, (char*)NULL);
            _exit(127);  // exec failed
        } else if (pid > 0) {
            printf("[MAIN] Startup process spawned (PID: %d)\n", pid);
        } else {
            perror("[MAIN] fork() failed");
        }
    }

    int rc = havel_wlr_run(server);
    havel_wlr_destroy(server);
    return rc;
}
