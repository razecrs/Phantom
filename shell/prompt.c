/*
 * prompt.c — phantom shell prompt engine
 *
 * I render the prompt entirely in C with zero subprocess calls.
 * Starship and oh-my-posh are great but they exec a new process every
 * prompt redraw. On a phone that's noticeable. I do it in-process:
 * read .git/HEAD directly, stat the battery sysfs node, check the
 * phantom socket — all in microseconds, no fork overhead.
 *
 * Config lives in ~/.phantom/prompt.toml. Themes are baked in as
 * C structs so there's nothing to parse at startup.
 */

#include "prompt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>

/* ── ANSI helpers ────────────────────────────────────────────── */
#define FG(r,g,b)  "\033[38;2;" #r ";" #g ";" #b "m"
#define BG(r,g,b)  "\033[48;2;" #r ";" #g ";" #b "m"
#define RESET      "\033[0m"
#define BOLD       "\033[1m"

static void append_fg(char *buf, size_t *pos, size_t max, rgb_t c) {
    *pos += snprintf(buf + *pos, max - *pos,
                     "\033[38;2;%d;%d;%dm", c.r, c.g, c.b);
}
static void append_bg(char *buf, size_t *pos, size_t max, rgb_t c) {
    *pos += snprintf(buf + *pos, max - *pos,
                     "\033[48;2;%d;%d;%dm", c.r, c.g, c.b);
}

/* ── segment renderers ───────────────────────────────────────── */

/* I shorten /very/long/path to ~/long/path or …/last/two/parts */
static void render_cwd(char *out, size_t max) {
    char cwd[512];
    getcwd(cwd, sizeof(cwd));
    const char *home = getenv("HOME");
    char tmp[512];
    if (home && strncmp(cwd, home, strlen(home)) == 0) {
        snprintf(tmp, sizeof(tmp), "~%s", cwd + strlen(home));
    } else {
        strncpy(tmp, cwd, sizeof(tmp));
    }
    /* truncate to last 2 components if too long */
    if (strlen(tmp) > 30) {
        char *p = tmp + strlen(tmp);
        int slashes = 0;
        while (p > tmp && slashes < 2) {
            if (*p == '/') slashes++;
            p--;
        }
        snprintf(out, max, "…%s", p + 1);
    } else {
        strncpy(out, tmp, max);
    }
}

/* I read .git/HEAD directly — no git subprocess, no 50ms lag */
static int render_git(char *out, size_t max) {
    char head_path[512];
    char cwd[512];
    getcwd(cwd, sizeof(cwd));

    /* walk up until I find .git or hit / */
    char *p = cwd + strlen(cwd);
    while (p >= cwd) {
        snprintf(head_path, sizeof(head_path), "%.*s/.git/HEAD",
                 (int)(p - cwd), cwd);
        FILE *f = fopen(head_path, "r");
        if (f) {
            char line[128];
            fgets(line, sizeof(line), f);
            fclose(f);
            /* ref: refs/heads/<branch> */
            const char *prefix = "ref: refs/heads/";
            if (strncmp(line, prefix, strlen(prefix)) == 0) {
                char *branch = line + strlen(prefix);
                branch[strcspn(branch, "\n")] = '\0';
                /* check for dirty working tree — fast: just stat index */
                char dirty_check[512];
                snprintf(dirty_check, sizeof(dirty_check),
                         "%.*s/.git/index", (int)(p - cwd), cwd);
                struct stat idx, head_stat;
                int dirty = 0;
                if (stat(dirty_check, &idx) == 0) {
                    snprintf(dirty_check, sizeof(dirty_check),
                             "%.*s/.git/COMMIT_EDITMSG", (int)(p-cwd), cwd);
                    if (stat(dirty_check, &head_stat) == 0)
                        dirty = (idx.st_mtime > head_stat.st_mtime);
                }
                snprintf(out, max, " %s%s", branch, dirty ? "*" : "");
            } else {
                /* detached HEAD — show short hash */
                line[7] = '\0';
                snprintf(out, max, " %.7s", line);
            }
            return 1;
        }
        if (p == cwd) break;
        while (p > cwd && *p != '/') p--;
    }
    return 0; /* not a git repo */
}

/* I read Android's battery sysfs — instant, no subprocess */
static int render_battery(char *out, size_t max) {
    const char *cap_path = "/sys/class/power_supply/battery/capacity";
    const char *sta_path = "/sys/class/power_supply/battery/status";
    FILE *f = fopen(cap_path, "r");
    if (!f) return 0;
    int pct = 0;
    fscanf(f, "%d", &pct);
    fclose(f);
    char status[32] = "?";
    f = fopen(sta_path, "r");
    if (f) { fscanf(f, "%31s", status); fclose(f); }
    const char *icon = pct > 80 ? "" : pct > 40 ? "" : pct > 20 ? "" : "";
    snprintf(out, max, "%s %d%%", icon, pct);
    return 1;
}

/* I ping the phantom socket to see if a module is attached */
static int phantom_is_active(const char *sock_path) {
    if (!sock_path) return 0;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);
    /* non-blocking connect — I don't want to hang the prompt */
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    int r = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    close(fd);
    return (r == 0 || errno == EINPROGRESS);
}

/* ── built-in themes ─────────────────────────────────────────── */

prompt_cfg_t prompt_theme_phantom_dark(void) {
    prompt_cfg_t cfg = {0};
    cfg.theme      = THEME_PHANTOM_DARK;
    cfg.nerd_fonts = 1;
    cfg.two_line   = 1;
    cfg.prompt_char_ok  = (rgb_t){0x59, 0xE3, 0x9F}; /* mint green  */
    cfg.prompt_char_err = (rgb_t){0xF8, 0x71, 0x71}; /* soft red    */

    /* segment 0: phantom status */
    cfg.segments[0] = (segment_t){
        .type = SEG_PH_STATUS,
        .fg   = {0x0A, 0x0A, 0x0A},
        .bg   = {0x8B, 0x5C, 0xF6}, /* violet */
        .powerline = 1,
        .icon = "",
    };
    /* segment 1: cwd */
    cfg.segments[1] = (segment_t){
        .type = SEG_CWD,
        .fg   = {0xFF, 0xFF, 0xFF},
        .bg   = {0x1E, 0x1E, 0x2E},
        .powerline = 1,
        .icon = "",
    };
    /* segment 2: git */
    cfg.segments[2] = (segment_t){
        .type = SEG_GIT,
        .fg   = {0xF5, 0xC2, 0xE7},
        .bg   = {0x31, 0x31, 0x4A},
        .powerline = 1,
        .icon = "",
    };
    /* segment 3: battery */
    cfg.segments[3] = (segment_t){
        .type = SEG_BATTERY,
        .fg   = {0xA6, 0xE3, 0xA1},
        .bg   = {0x1E, 0x1E, 0x2E},
        .powerline = 0,
    };
    cfg.nsegments = 4;
    return cfg;
}

prompt_cfg_t prompt_theme_minimal(void) {
    prompt_cfg_t cfg = {0};
    cfg.theme      = THEME_MINIMAL;
    cfg.nerd_fonts = 0;
    cfg.two_line   = 0;
    cfg.prompt_char_ok  = (rgb_t){0x59, 0xE3, 0x9F};
    cfg.prompt_char_err = (rgb_t){0xF8, 0x71, 0x71};
    cfg.segments[0] = (segment_t){.type=SEG_CWD,  .fg={200,200,200}, .powerline=0};
    cfg.segments[1] = (segment_t){.type=SEG_GIT,  .fg={180,140,250}, .powerline=0};
    cfg.nsegments = 2;
    return cfg;
}

prompt_cfg_t prompt_theme_retro(void) {
    prompt_cfg_t cfg = {0};
    cfg.theme      = THEME_RETRO;
    cfg.nerd_fonts = 0;
    cfg.two_line   = 0;
    cfg.prompt_char_ok  = (rgb_t){0x00, 0xFF, 0x00};
    cfg.prompt_char_err = (rgb_t){0xFF, 0x00, 0x00};
    /* green-on-black like an old terminal */
    cfg.segments[0] = (segment_t){.type=SEG_USER, .fg={0,255,0}, .powerline=0};
    cfg.segments[1] = (segment_t){.type=SEG_HOST, .fg={0,200,0}, .powerline=0};
    cfg.segments[2] = (segment_t){.type=SEG_CWD,  .fg={0,255,0}, .powerline=0};
    cfg.nsegments = 3;
    return cfg;
}

/* ── main renderer ───────────────────────────────────────────── */

void prompt_render(const prompt_cfg_t *cfg, char *buf, size_t bufsz,
                   int last_exit, const char *phantom_sock) {
    size_t pos = 0;
    char tmp[256];

    for (int i = 0; i < cfg->nsegments; i++) {
        const segment_t *seg = &cfg->segments[i];

        /* set colors */
        if (seg->bg.r || seg->bg.g || seg->bg.b)
            append_bg(buf, &pos, bufsz, seg->bg);
        append_fg(buf, &pos, bufsz, seg->fg);

        /* icon */
        if (cfg->nerd_fonts && seg->icon[0])
            pos += snprintf(buf+pos, bufsz-pos, " %s", seg->icon);

        /* content */
        switch (seg->type) {
        case SEG_USER: {
            const char *u = getenv("USER");
            pos += snprintf(buf+pos, bufsz-pos, " %s ", u ? u : "phantom");
            break;
        }
        case SEG_HOST: {
            char h[64]; gethostname(h, sizeof(h));
            pos += snprintf(buf+pos, bufsz-pos, "@%s ", h);
            break;
        }
        case SEG_CWD:
            render_cwd(tmp, sizeof(tmp));
            pos += snprintf(buf+pos, bufsz-pos, " %s ", tmp);
            break;
        case SEG_GIT:
            if (render_git(tmp, sizeof(tmp)))
                pos += snprintf(buf+pos, bufsz-pos, "%s ", tmp);
            break;
        case SEG_BATTERY:
            if (render_battery(tmp, sizeof(tmp)))
                pos += snprintf(buf+pos, bufsz-pos, " %s ", tmp);
            break;
        case SEG_TIME: {
            time_t t = time(NULL);
            struct tm *tm = localtime(&t);
            pos += snprintf(buf+pos, bufsz-pos, " %02d:%02d ", tm->tm_hour, tm->tm_min);
            break;
        }
        case SEG_EXIT:
            if (last_exit != 0)
                pos += snprintf(buf+pos, bufsz-pos, " ✗%d ", last_exit);
            break;
        case SEG_PH_STATUS:
            if (phantom_is_active(phantom_sock))
                pos += snprintf(buf+pos, bufsz-pos, " ● ");
            else
                pos += snprintf(buf+pos, bufsz-pos, " ○ ");
            break;
        case SEG_CUSTOM:
            pos += snprintf(buf+pos, bufsz-pos, " %s ", seg->custom_text);
            break;
        }

        /* powerline separator — print in next segment's bg */
        if (seg->powerline && i < cfg->nsegments - 1) {
            const segment_t *next = &cfg->segments[i+1];
            append_fg(buf, &pos, bufsz, seg->bg);   /* current bg as fg */
            append_bg(buf, &pos, bufsz, next->bg);  /* next bg */
            pos += snprintf(buf+pos, bufsz-pos, "\xee\x82\xb0");
        }
    }

    /* reset + newline if two_line mode */
    pos += snprintf(buf+pos, bufsz-pos, "%s", RESET);
    if (cfg->two_line)
        pos += snprintf(buf+pos, bufsz-pos, "\n");

    /* prompt character — color depends on last exit code */
    rgb_t pc = last_exit == 0 ? cfg->prompt_char_ok : cfg->prompt_char_err;
    append_fg(buf, &pos, bufsz, pc);
    pos += snprintf(buf+pos, bufsz-pos, "\u22eb %s", RESET);
}
