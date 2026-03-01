#include <wm/wlr_bridge.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void print_usage(const char* prog) {
    printf("Usage: %s [OPTIONS]\n", prog);
    printf("\nOptions:\n");
    printf("  -s, --startup <command>  Command to execute on startup\n");
    printf("  -h, --help               Show this help message\n");
    printf("\nExamples:\n");
    printf("  %s -s 'foot'             Start foot terminal on launch\n", prog);
    printf("  %s -s 'swaybg -i bg.png' Start with wallpaper\n", prog);
}

int main(int argc, char* argv[]) {
    const char* startup_cmd = NULL;

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
