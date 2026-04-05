#pragma once
#include <stddef.h>
#include <stdint.h>

/*
 * Phantom field dictionary — i use this to auto-detect modifiable fields
 * in intercepted traffic without the user having to read raw JSON for hours.
 *
 * Two top-level categories: GAME and APP.
 * Each has sub-categories so the UI can group and label hits clearly.
 *
 * Matching is case-insensitive substring match on the field KEY, not value.
 * e.g. "softCurrency" matches pattern "currency".
 */

typedef enum {
    /* ── game categories ─────────────────────────── */
    FIELD_GAME_SOFT_CURRENCY = 0,   /* farmable in-game money */
    FIELD_GAME_HARD_CURRENCY,       /* paid/premium currency */
    FIELD_GAME_ENERGY,              /* stamina / lives / moves */
    FIELD_GAME_PROGRESSION,         /* xp / level / rank / score */
    FIELD_GAME_INVENTORY,           /* crafting materials / resources */
    FIELD_GAME_SOCIAL,              /* friends / followers / fame */
    FIELD_GAME_UNLOCK,              /* locked / owned / purchased flags */
    FIELD_GAME_BATTLE,              /* trophies / elo / league */
    FIELD_GAME_SEASON,              /* season pass / battle pass */
    FIELD_GAME_GACHA,               /* pulls / pity / summon count */

    /* ── app categories ──────────────────────────── */
    FIELD_APP_SUBSCRIPTION,         /* premium / plan / membership tier */
    FIELD_APP_FEATURE_FLAG,         /* can_download / offline_mode / hifi */
    FIELD_APP_CONTENT_GATE,         /* locked / paywalled / restricted */
    FIELD_APP_LIMITS,               /* skip_limit / daily_limit / quota */
    FIELD_APP_TRIAL,                /* trial_days / is_trial / expired */
    FIELD_APP_ADS,                  /* ad_free / show_ads / ad_interval */
    FIELD_APP_QUALITY,              /* bitrate / resolution / quality tier */
    FIELD_APP_SOCIAL,               /* followers / likes / reputation */
    FIELD_APP_COMMERCE,             /* balance / wallet / credits / store */

    FIELD_CATEGORY_COUNT
} field_category_t;

typedef struct {
    const char       *pattern;      /* lowercase substring to match in key  */
    field_category_t  category;
    const char       *label;        /* human-readable label shown in UI     */
} field_entry_t;

/* i keep these sorted by category then alphabetically within each group */
extern const field_entry_t PHANTOM_FIELD_DICT[];
extern const size_t        PHANTOM_FIELD_DICT_LEN;

/* label strings for each category */
extern const char *FIELD_CATEGORY_LABELS[FIELD_CATEGORY_COUNT];

/* mode flags for select-mode scanning */
#define SCAN_MODE_GAMES  (1u << 0)
#define SCAN_MODE_APPS   (1u << 1)
#define SCAN_MODE_ALL    (SCAN_MODE_GAMES | SCAN_MODE_APPS)
