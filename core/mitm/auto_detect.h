#pragma once
#include "field_dict.h"
#include <stdint.h>
#include <stddef.h>

/*
 * Select-mode field scanner.
 *
 * i NEVER run automatically — the user explicitly calls
 * Network.analyze() or ph analyze <pkg> to trigger a scan.
 *
 * i record what the field ACTUALLY returns across multiple responses
 * so the companion app can show the real value + type and let the
 * user decide what to replace it with — no hardcoded defaults.
 *
 * Value types cover every real-world case games/apps return:
 *   number  → coins, stamina, level
 *   bool    → is_premium, is_locked
 *   int_bool→ purchased: 0/1, active: 0/1
 *   string  → status: "failed"/"success", result: "NOT_ENOUGH_GOLD"/"OK"
 */

#define MAX_HITS        256
#define MAX_PATH_LEN    256
#define MAX_VALUE_LEN   128
#define MAX_SEEN        4     /* track up to 4 distinct values per field */

typedef enum {
    VTYPE_NUMBER   = 0,   /* 1240, 0, -5.5, 99.9          */
    VTYPE_BOOL     = 1,   /* true / false (JSON bool)      */
    VTYPE_INT_BOOL = 2,   /* 0 or 1 used as boolean        */
    VTYPE_STRING   = 3,   /* "success", "locked", "active" */
    VTYPE_UNKNOWN  = 4,
} value_type_t;

typedef struct {
    char             json_path[MAX_PATH_LEN]; /* e.g. "data.wallet.soft_currency"   */
    char             url[512];                /* endpoint that returned this field   */
    char             raw_value[MAX_VALUE_LEN];/* last observed value as string       */
    field_category_t category;
    const char      *label;                   /* human-readable name for UI          */
    value_type_t     vtype;                   /* determines which UI control to show */
    uint32_t         hit_count;               /* times this path was seen            */

    /*
     * i collect up to MAX_SEEN distinct raw values across multiple
     * responses so the companion app can show the user what the field
     * actually changes between — e.g. "failed" / "success" / "pending"
     * for a status field, or 1200/1240/2000 for a coin counter.
     * The user picks their replacement value from context, not guesswork.
     */
    char             seen_values[MAX_SEEN][MAX_VALUE_LEN];
    uint8_t          seen_count;
} field_hit_t;

typedef struct {
    field_hit_t hits[MAX_HITS];
    uint32_t    count;
    uint32_t    scan_mode;    /* SCAN_MODE_GAMES | SCAN_MODE_APPS | SCAN_MODE_ALL */
} scan_result_t;

/*
 * Scan a JSON body for fields matching the dictionary.
 * mode : SCAN_MODE_GAMES, SCAN_MODE_APPS, or SCAN_MODE_ALL
 * url  : endpoint this body came from — stored in hits for UI display
 * Call repeatedly across multiple captured responses — results
 * deduplicate by json_path and accumulate seen_values.
 */
void phantom_scan_json(const char    *body,
                       size_t         len,
                       const char    *url,
                       uint32_t       mode,
                       scan_result_t *result);

/*
 * Serialize result to JSON string for ring buffer / companion app.
 * Caller must free() the returned pointer.
 */
char *phantom_scan_result_to_json(const scan_result_t *result);

/* reset a result set for a fresh scan */
void phantom_scan_result_reset(scan_result_t *result);
