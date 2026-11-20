#include <wm/wlr_bridge.h>
#include <wm/RendererSelection.hpp>
#include <core/LoadingScreen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

static void print_usage(const char* prog) {
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("\nOptions:\n");
    printf("  -s, --startup <command>  Command to execute on startup\n");
    printf("  --no-loading-screen      Disable loading screen\n");
    printf("  -r, --renderer <backend> Select wlroots renderer backend:\n");
    printf("                             auto     - Auto-detect (default)\n");
    printf("                             gles2    - Force OpenGL ES 2\n");
    printf("                             opengl   - Alias for gles2\n");
    printf("                             vulkan   - Force Vulkan\n");
    printf("                             wlroots  - Alias for auto\n");
    printf("  -h, --help               Show this help message\n");
    printf("\nNote: wlroots handles all window rendering. Custom buffer import\n");
    printf("(SHM/DMA-BUF) is available for plugins needing texture access.\n");
    printf("\nExamples:\n");
    printf("  %s -s 'foot'             Start foot terminal on launch\n", prog);
    printf("  %s -r vulkan -s 'swaybg' Use Vulkan backend with wallpaper\n", prog);
    printf("  %s -r opengl             Force OpenGL ES 2 backend\n", prog);
    printf("  %s --no-loading-screen   Start without loading screen\n", prog);
}

int main(int argc, char* argv[]) {
    // Line-buffer stdout so plugin/engine logging survives a redirect.
    // printf output was block-buffered when stdout was a file, so the last
    // few KB of diagnostics (Alt-Tab's showing/hidden lines, scene state)
    // were lost whenever the compositor was killed rather than exited -
    // which is exactly how it is run for testing and crash reports.
    setvbuf(stdout, NULL, _IOLBF, 0);

    const char* startup_cmd = NULL;
    bool loading_screen_enabled = true;
    havel::RendererBackend renderer = havel::RendererBackend::Auto;

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
                if (!havel::parseRendererBackend(argv[i], &renderer)) {
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

    // wlroots reads WLR_RENDERER when wlr_renderer_autocreate() runs inside
    // havel_wlr_create(). Configure it before creating the server so an
    // explicit renderer choice is actually honored.
    const char* wlr_renderer = havel::rendererEnvironmentValue(renderer);
    if (wlr_renderer) {
        if (setenv("WLR_RENDERER", wlr_renderer, 1) != 0) {
            fprintf(stderr, "Error: failed to set WLR_RENDERER=%s: %s\n",
                    wlr_renderer, strerror(errno));
            return 1;
        }
        printf("[MAIN] WLR_RENDERER=%s selected before wlroots renderer creation\n",
               wlr_renderer);
    }

    havel_wlr_server_t* server = havel_wlr_create();
    if (!server) {
        return 1;
    }

    // Disable loading screen if requested
    if (!loading_screen_enabled) {
        struct LoadingScreenConfig* config = loading_screen_get_config();
        config->enabled = false;
    }

    // Execute startup command if specified
    if (startup_cmd) {
        printf("[MAIN] Executing startup command: %s\n", startup_cmd);
        char* cmd_copy = strdup(startup_cmd);
        if (!cmd_copy) {
            perror("[MAIN] strdup failed");
        } else {
            char* argv[64];
            int argc = 0;
            char* token = strtok(cmd_copy, " \t\n");
            while (token && argc < 63) {
                argv[argc++] = token;
                token = strtok(NULL, " \t\n");
            }
            argv[argc] = NULL;
            
            if (argc > 0) {
                pid_t pid = fork();
                if (pid == 0) {
                    // Child process - execute command directly, no shell
                    execvp(argv[0], argv);
                    _exit(127);  // exec failed
                } else if (pid > 0) {
                    printf("[MAIN] Startup process spawned (PID: %d)\n", pid);
                } else {
                    perror("[MAIN] fork() failed");
                }
            }
            free(cmd_copy);
        }
    }

    int rc = havel_wlr_run(server);
    havel_wlr_destroy(server);
    return rc;
}
