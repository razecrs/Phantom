#pragma once
#include <stdint.h>
#include <stddef.h>

/*
 * intercept_engine -- applies registered patch rules to JSON response bodies.
 *
 * A patch rule says:
 *   "whenever the URL matches `url_pattern`, set JSON field at `json_path`
 *    to `value_str` before the response reaches the app."
 *
 * Rules are registered from JS (Network.patch / Game.pinField) and from
 * the companion app. They persist until explicitly removed.
 *
 * The engine does an in-place text substitution on the JSON body — it
 * does NOT parse the full document, just replaces the value token that
 * follows the matched key path. This is intentionally simple and fast.
 * The scanner (auto_detect.c) already told us exact json_paths; we just
 * need to swap the value.
 */

#define MAX_RULES         256
#define RULE_PATH_LEN     256
#define RULE_VALUE_LEN    128
#define RULE_PATTERN_LEN  512

typedef struct {
    char     json_path[RULE_PATH_LEN];    /* e.g. "data.wallet.soft_currency" */
    char     value_str[RULE_VALUE_LEN];   /* e.g. "2147483647" or "\"premium\"" */
    char     url_pattern[RULE_PATTERN_LEN]; /* glob: "*!/sync", "*" = all */
    uint8_t  active;
    uint32_t id;                          /* unique rule id for removal */
} patch_rule_t;

typedef struct {
    patch_rule_t rules[MAX_RULES];
    uint32_t     count;
    uint32_t     next_id;
} intercept_ctx_t;

/* Initialize the context */
void intercept_ctx_init(intercept_ctx_t *ctx);

/* Add a rule. Returns rule id (use to remove), or 0 on failure. */
uint32_t intercept_add_rule(intercept_ctx_t *ctx,
                             const char *json_path,
                             const char *value_str,
                             const char *url_pattern);

/* Remove rule by id */
void intercept_remove_rule(intercept_ctx_t *ctx, uint32_t id);

/*
 * Apply matching rules to `body`.
 * Returns a heap-allocated modified copy (caller must free) if any rule
 * triggered, or NULL if no modification was needed.
 *
 * url: the request URL, used to match url_pattern.
 */
char *intercept_apply(intercept_ctx_t *ctx,
                      const char      *url,
                      const char      *body,
                      size_t           body_len,
                      size_t          *out_len);
