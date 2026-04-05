/*
 * psh — phantom shell
 *
 * I wrote this because I wanted a shell that lives inside my Arch ARM
 * rootfs on Android and feels exactly like desktop Arch, with built-in
 * phantom RE commands baked in. No Termux dependency, no pkg wrapper —
 * just yay/paru talking directly to Arch ARM repos like I'm on a real machine.
 *
 * How it works:
 *   - Interactive mode: prompt → tokenize → dispatch
 *   - psh -S nodejs        → yay -S nodejs (Arch ARM repo)
 *   - psh -c "ph attach x" → run one command and exit
 *   - ph commands          → forward to phantom module via Unix socket
 *   - everything else      → execvp, falls through to PATH
 *
 * I keep PHANTOM_ROOT pointing at my Arch rootfs. Commands that need
 * the full Linux userland run via chroot into that rootfs automatically.
 */

#include "prompt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <signal.h>
#include <dirent.h>

#define PSH_VERSION     "0.1.0"
#define PHANTOM_SOCK    "/dev/phantom/control.sock"
#define PHANTOM_ROOT    "/data/phantom/rootfs"
#define PHANTOM_BIN     "/data/phantom/bin"
#define MAX_ARGS         64
#define PSH_INPUT_MAX  4096
#undef  MAX_INPUT
#define MAX_INPUT      PSH_INPUT_MAX

static prompt_cfg_t g_prompt_cfg;
static int g_last_exit = 0;

/* ── prompt ─────────────────────────────────────────────────── */
static void print_prompt(void) {
    char buf[1024];
    prompt_render(&g_prompt_cfg, buf, sizeof(buf), g_last_exit, PHANTOM_SOCK);
    fputs(buf, stdout);
    fflush(stdout);
}

/* ── talk to phantom module via Unix socket ─────────────────── */
/* I pipe commands to the module and stream its response back.   */
static int ph_send(const char *cmd) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { perror("ph: socket"); return 1; }

    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    strncpy(addr.sun_path, PHANTOM_SOCK, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "ph: phantom module not running — is ZygiskNext loaded?\n");
        close(fd);
        return 1;
    }
    write(fd, cmd, strlen(cmd));
    write(fd, "\n", 1);

    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        fputs(buf, stdout);
    }
    close(fd);
    return 0;
}

/* exec a binary from PHANTOM_BIN, passing remaining argv */
static int exec_bin(const char *name, int argc, char **argv) {
    char bin[256];
    snprintf(bin, sizeof(bin), "%s/%s", PHANTOM_BIN, name);
    if (access(bin, X_OK) != 0) {
        fprintf(stderr, "ph: %s not found — reinstall Phantom\n", bin);
        return 1;
    }
    argv[0] = bin;
    execv(bin, argv);
    perror("execv");
    return 1;
}

static int daemon_pid(void) {
    FILE *f = fopen("/data/phantom/daemon.pid", "r");
    if (!f) return -1;
    int pid = -1;
    fscanf(f, "%d", &pid);
    fclose(f);
    return pid;
}

static int cmd_ph(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
            "Phantom " PSH_VERSION "\n"
            "usage: ph <command> [args]\n"
            "\n"
            "Traffic / MITM:\n"
            "  traffic live [--host HOST:PORT]  open live ANSI traffic viewer\n"
            "  traffic log                       print traffic as JSON lines\n"
            "  traffic scan                      run field scanner, print hits\n"
            "  patch <path> <value> [url]        add a patch rule\n"
            "\n"
            "Module:\n"
            "  attach <pkg>                      attach to running app\n"
            "  trace  <pkg>                      live method tracer\n"
            "  dump   <pkg>                      dump in-memory DEX\n"
            "  scan   <pkg> <val>                memory scanner\n"
            "  hook   <script.js>                load hook into attached app\n"
            "  repl   <pkg>                      live JS REPL\n"
            "\n"
            "Daemon:\n"
            "  daemon start                      start phantom-daemon\n"
            "  daemon stop                       stop phantom-daemon\n"
            "  daemon status                     show daemon status\n"
            "\n"
            "  update                            update phantom platform\n"
        );
        return 1;
    }

    const char *sub = argv[1];

    /* ── traffic subcommands ─────────────────────────────────── */
    if (strcmp(sub, "traffic") == 0) {
        if (argc < 3 || strcmp(argv[2], "live") == 0) {
            /* exec phantom-hub with remaining args */
            /* argv[0]=ph argv[1]=traffic argv[2]=live [argv[3]=--host ...] */
            char *hub_argv[MAX_ARGS];
            char bin[256];
            snprintf(bin, sizeof(bin), "%s/ph-hub", PHANTOM_BIN);
            hub_argv[0] = bin;
            int j = 1;
            for (int i = 3; i < argc && j < MAX_ARGS - 1; i++)
                hub_argv[j++] = argv[i];
            hub_argv[j] = NULL;
            if (access(bin, X_OK) != 0) {
                fprintf(stderr, "ph: ph-hub not found — reinstall Phantom\n");
                return 1;
            }
            execv(bin, hub_argv);
            perror("execv hub");
            return 1;
        }
        if (strcmp(argv[2], "log") == 0) {
            /* curl-style SSE dump */
            const char *host = argc > 3 ? argv[3] : "localhost:7777";
            char url[256];
            snprintf(url, sizeof(url), "http://%s/events", host);
            execlp("curl", "curl", "-sN", url, NULL);
            perror("execv curl");
            return 1;
        }
        if (strcmp(argv[2], "scan") == 0) {
            const char *host = argc > 3 ? argv[3] : "localhost:7777";
            char url[256];
            snprintf(url, sizeof(url), "http://%s/scan", host);
            execlp("curl", "curl", "-sX", "POST", url, NULL);
            perror("execv curl");
            return 1;
        }
        fprintf(stderr, "ph traffic: unknown subcommand '%s'\n", argv[2]);
        return 1;
    }

    /* ── patch rule ──────────────────────────────────────────── */
    if (strcmp(sub, "patch") == 0) {
        if (argc < 4) {
            fprintf(stderr, "usage: ph patch <json.path> <value> [url_glob]\n");
            return 1;
        }
        const char *path  = argv[2];
        const char *value = argv[3];
        const char *url   = argc > 4 ? argv[4] : "*";
        char body[1024];
        snprintf(body, sizeof(body),
                 "{\"path\":\"%s\",\"value\":\"%s\",\"url\":\"%s\"}",
                 path, value, url);
        char cmd[2048];
        snprintf(cmd, sizeof(cmd),
                 "curl -s -X POST http://localhost:7777/patch "
                 "-H 'Content-Type: application/json' -d '%s'", body);
        return system(cmd);
    }

    /* ── daemon subcommands ──────────────────────────────────── */
    if (strcmp(sub, "daemon") == 0) {
        if (argc < 3 || strcmp(argv[2], "status") == 0) {
            int pid = daemon_pid();
            if (pid > 0) {
                char proc[64];
                snprintf(proc, sizeof(proc), "/proc/%d", pid);
                if (access(proc, F_OK) == 0)
                    printf("phantom-daemon running (pid %d)\n", pid);
                else
                    printf("phantom-daemon dead (stale pid %d)\n", pid);
            } else {
                printf("phantom-daemon not running\n");
            }
            return 0;
        }
        if (strcmp(argv[2], "start") == 0) {
            char bin[256];
            snprintf(bin, sizeof(bin), "%s/phantom-daemon", PHANTOM_BIN);
            if (access(bin, X_OK) != 0) {
                fprintf(stderr, "ph: phantom-daemon not found\n");
                return 1;
            }
            /* fork + exec daemon in background */
            pid_t pid = fork();
            if (pid == 0) {
                setsid();
                freopen("/data/phantom/daemon.log", "a", stdout);
                freopen("/data/phantom/daemon.log", "a", stderr);
                execlp(bin, bin, NULL);
                _exit(1);
            }
            if (pid > 0) {
                FILE *f = fopen("/data/phantom/daemon.pid", "w");
                if (f) { fprintf(f, "%d\n", pid); fclose(f); }
                printf("phantom-daemon started (pid %d)\n", pid);
            }
            return 0;
        }
        if (strcmp(argv[2], "stop") == 0) {
            int pid = daemon_pid();
            if (pid <= 0) { printf("daemon not running\n"); return 0; }
            kill(pid, SIGTERM);
            remove("/data/phantom/daemon.pid");
            printf("phantom-daemon stopped\n");
            return 0;
        }
        fprintf(stderr, "ph daemon: unknown subcommand '%s'\n", argv[2]);
        return 1;
    }

    /* ── fallback: forward to control socket ─────────────────── */
    char cmd[MAX_INPUT] = {0};
    for (int i = 1; i < argc; i++) {
        strncat(cmd, argv[i], sizeof(cmd) - strlen(cmd) - 2);
        if (i < argc - 1)
            strncat(cmd, " ", sizeof(cmd) - strlen(cmd) - 1);
    }
    return ph_send(cmd);
}

/* ── yay/paru — I locate whichever is installed and exec it ─── */
static int cmd_pkg(int argc, char **argv) {
    /* look inside the Arch rootfs first, then PATH */
    char paths[3][256];
    snprintf(paths[0], sizeof(paths[0]), "%s/usr/bin/yay",  PHANTOM_ROOT);
    snprintf(paths[1], sizeof(paths[1]), "%s/usr/bin/paru", PHANTOM_ROOT);
    snprintf(paths[2], sizeof(paths[2]), "yay"); /* fallback: PATH */

    char *bin = NULL;
    for (int i = 0; i < 3; i++) {
        if (access(paths[i], X_OK) == 0) { bin = paths[i]; break; }
    }
    if (!bin) {
        fprintf(stderr, "psh: yay/paru not found — run: phantom-bootstrap\n");
        return 1;
    }
    argv[0] = bin;

    /* I chroot into the Arch rootfs so yay sees the right filesystem */
    if (chdir(PHANTOM_ROOT) == 0) {
        chroot(PHANTOM_ROOT);
        chdir("/");
    }
    execv(bin, argv);
    perror("execv");
    return 1;
}

/* ── builtins ────────────────────────────────────────────────── */
static int builtin_cd(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : getenv("HOME");
    if (!path) path = "/";
    if (chdir(path) != 0) { perror("cd"); return 1; }
    return 0;
}

static int builtin_exit(int argc, char **argv) {
    exit(argc > 1 ? atoi(argv[1]) : 0);
}

/* show what I can do */
static int builtin_help(void) {
    puts(
        "psh v" PSH_VERSION " — phantom shell\n"
        "\n"
        "RE ENGINE:\n"
        "  ph attach <pkg>       attach RE engine to a running app\n"
        "  ph trace  <pkg>       live method tracer\n"
        "  ph dump   <pkg>       dump in-memory DEX\n"
        "  ph hook   <script>    load a JS hook script\n"
        "  ph repl   <pkg>       interactive JS REPL\n"
        "\n"
        "PACKAGE MANAGER:\n"
        "  psh -S <pkg>          install from Arch ARM (yay -S)\n"
        "  psh -R <pkg>          remove package\n"
        "  psh -Syu              full system upgrade\n"
        "  psh -Ss <query>       search packages\n"
        "\n"
        "SHELL BUILTINS:\n"
        "  cd <path>             change directory\n"
        "  help                  show this message\n"
        "  exit                  exit psh\n"
        "\n"
        "Everything else is passed to the shell PATH as-is."
    );
    return 0;
}

/* ── external command — fork + exec ─────────────────────────── */
static int exec_external(int argc, char **argv) {
    pid_t pid = fork();
    if (pid == 0) {
        execvp(argv[0], argv);
        fprintf(stderr, "psh: %s: command not found\n", argv[0]);
        exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

/* ── tokenizer ───────────────────────────────────────────────── */
static int tokenize(char *line, char **argv, int max) {
    int argc = 0;
    char *tok = strtok(line, " \t\n");
    while (tok && argc < max - 1) {
        argv[argc++] = tok;
        tok = strtok(NULL, " \t\n");
    }
    argv[argc] = NULL;
    return argc;
}

/* ── dispatch ────────────────────────────────────────────────── */
static int dispatch(int argc, char **argv) {
    if (argc == 0) return 0;
    const char *cmd = argv[0];

    if (strcmp(cmd, "cd")     == 0) return builtin_cd(argc, argv);
    if (strcmp(cmd, "exit")   == 0) return builtin_exit(argc, argv);
    if (strcmp(cmd, "help")   == 0) return builtin_help();
    if (strcmp(cmd, "ph")     == 0) return cmd_ph(argc, argv);
    if (strcmp(cmd, "yay")    == 0) return cmd_pkg(argc, argv);
    if (strcmp(cmd, "paru")   == 0) return cmd_pkg(argc, argv);

    /* psh -S / -R / -Syu / -Ss … */
    if (strcmp(cmd, "psh") == 0 && argc > 1 && argv[1][0] == '-')
        return cmd_pkg(argc - 1, argv + 1);

    return exec_external(argc, argv);
}

/* ── entry ───────────────────────────────────────────────────── */
int main(int argc, char **argv) {
    /* psh -c "command" — non-interactive one-shot */
    if (argc >= 3 && strcmp(argv[1], "-c") == 0) {
        char line[MAX_INPUT];
        strncpy(line, argv[2], sizeof(line) - 1);
        char *args[MAX_ARGS];
        int n = tokenize(line, args, MAX_ARGS);
        return dispatch(n, args);
    }

    /* psh -S nodejs → yay -S nodejs */
    if (argc >= 2 && argv[1][0] == '-') {
        /* rebuild argv starting from the flag */
        return cmd_pkg(argc - 1, argv + 1);
    }

    /* interactive */
    printf("\033[1;35mPhantom Shell\033[0m v%s  |  type 'help' for commands\n",
           PSH_VERSION);

    g_prompt_cfg = prompt_theme_phantom_dark();

    signal(SIGINT, SIG_IGN);

    char  line[MAX_INPUT];
    char *args[MAX_ARGS];

    for (;;) {
        print_prompt();
        if (!fgets(line, sizeof(line), stdin)) { putchar('\n'); break; }
        int n = tokenize(line, args, MAX_ARGS);
        g_last_exit = dispatch(n, args);
    }
    return 0;
}
