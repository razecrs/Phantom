#pragma once
#include <stdint.h>

/* 24-bit RGB color */
typedef struct { uint8_t r, g, b; } rgb_t;

/* prompt segment types — each renders one piece of the prompt */
typedef enum {
    SEG_USER,        /* username */
    SEG_HOST,        /* hostname */
    SEG_CWD,         /* current directory, shortened */
    SEG_GIT,         /* git branch + dirty flag */
    SEG_TIME,        /* HH:MM:SS */
    SEG_EXIT,        /* last exit code — only shows on non-zero */
    SEG_BATTERY,     /* battery % — only on Android */
    SEG_PH_STATUS,   /* phantom module attached / count of active hooks */
    SEG_CUSTOM,      /* user-defined text + color from config */
} seg_type_t;

typedef struct {
    seg_type_t type;
    rgb_t      fg;
    rgb_t      bg;
    int        powerline;   /* 1 = use ▶ separator, 0 = plain space */
    char       icon[8];     /* nerd font icon UTF-8, empty = no icon */
    char       custom_text[64]; /* SEG_CUSTOM content */
} segment_t;

typedef enum {
    THEME_PHANTOM_DARK,
    THEME_PHANTOM_LIGHT,
    THEME_MINIMAL,
    THEME_POWERLINE,
    THEME_RETRO,
    THEME_CUSTOM,
} theme_id_t;

typedef struct {
    theme_id_t  theme;
    segment_t   segments[16];
    int         nsegments;
    int         two_line;      /* prompt on its own line, input below */
    int         nerd_fonts;    /* icons enabled */
    rgb_t       prompt_char_ok;   /* color of » when last cmd succeeded */
    rgb_t       prompt_char_err;  /* color of » when last cmd failed */
} prompt_cfg_t;

/* load config from ~/.phantom/prompt.toml, fills cfg */
int  prompt_load_config(prompt_cfg_t *cfg, const char *path);

/* render full prompt string into buf (ANSI escape codes included) */
void prompt_render(const prompt_cfg_t *cfg, char *buf, size_t bufsz,
                   int last_exit, const char *phantom_sock);

/* built-in themes — returns a ready-to-use config */
prompt_cfg_t prompt_theme_phantom_dark(void);
prompt_cfg_t prompt_theme_minimal(void);
prompt_cfg_t prompt_theme_powerline(void);
prompt_cfg_t prompt_theme_retro(void);
