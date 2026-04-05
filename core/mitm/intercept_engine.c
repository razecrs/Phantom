#include "intercept_engine.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <android/log.h>

#define TAG "Phantom/Intercept"

/* ---- simple glob matcher ------------------------------------------------
 * Supports:
 *   *   matches any sequence of characters
 *   ?   matches any single character
 * Case-insensitive on the URL.
 */
static int glob_match(const char *pat, const char *str) {
    while (*pat && *str) {
        if (*pat == '*') {
            pat++;
            if (!*pat) return 1; /* trailing * matches everything */
            while (*str) {
                if (glob_match(pat, str)) return 1;
                str++;
            }
            return 0;
        }
        if (*pat == '?' || tolower((unsigned char)*pat) == tolower((unsigned char)*str)) {
            pat++; str++;
        } else {
            return 0;
        }
    }
    while (*pat == '*') pat++;
    return (*pat == '\0' && *str == '\0');
}

/* ---- find the value token for a dotted JSON path -------------------------
 *
 * Given body = { "data": { "wallet": { "coins": 1200 } } }
 * and path  = "data.wallet.coins"
 * returns a pointer to the '1' in "coins": 1200, and sets *vlen to the
 * length of the value token ("1200" → 4).
 *
 * Strategy: split path on '.', find each key in order, stop at the
 * final key and return a pointer to its value.
 *
 * Limitations (acceptable for our use-case):
 *   - Doesn't handle arrays (path with []) — field_hit paths use .key only
 *   - Doesn't handle escaped quote characters in string values
 *   - O(n) per path segment — fine for ≤8 levels and ≤1MB bodies
 */
static const char *find_value_in_json(const char *json, size_t json_len,
                                       const char *dotted_path,
                                       size_t     *val_start_offset,
                                       size_t     *val_len) {
    char path_copy[RULE_PATH_LEN];
    strncpy(path_copy, dotted_path, RULE_PATH_LEN - 1);
    path_copy[RULE_PATH_LEN - 1] = '\0';

    const char *search = json;
    size_t      search_len = json_len;
    char       *segment    = path_copy;

    while (segment && *segment) {
        char *next = strchr(segment, '.');
        if (next) *next = '\0';

        /* find "segment": in the current search window */
        size_t slen = strlen(segment);
        int found = 0;
        for (size_t i = 0; i + slen + 3 < search_len; i++) {
            if (search[i] != '"') continue;
            if (strncmp(search + i + 1, segment, slen) != 0) continue;
            if (search[i + 1 + slen] != '"') continue;

            /* found key, skip past ": " */
            size_t pos = i + 1 + slen + 1;
            while (pos < search_len && (search[pos] == ':' ||
                   search[pos] == ' ' || search[pos] == '\t' ||
                   search[pos] == '\n' || search[pos] == '\r')) pos++;
            if (pos >= search_len) break;

            if (!next) {
                /* this is the final segment — point at value */
                const char *vstart = search + pos;
                /* measure token length */
                size_t vlen_local = 0;
                if (*vstart == '"') {
                    vlen_local = 1;
                    while (vstart[vlen_local] && vstart[vlen_local] != '"') {
                        if (vstart[vlen_local] == '\\') vlen_local++;
                        vlen_local++;
                    }
                    vlen_local++; /* closing quote */
                } else {
                    while (vstart[vlen_local] && vstart[vlen_local] != ',' &&
                           vstart[vlen_local] != '}' && vstart[vlen_local] != ']' &&
                           vstart[vlen_local] != ' ' && vstart[vlen_local] != '\n')
                        vlen_local++;
                }
                *val_start_offset = (size_t)(vstart - json);
                *val_len = vlen_local;
                return vstart;
            } else {
                /* recurse into the nested object starting from this position */
                search     = search + pos;
                search_len = search_len - pos;
                found = 1;
                break;
            }
        }
        if (!next) break;
        if (!found) return NULL;
        segment = next + 1;
    }
    return NULL;
}

/* ---- replace all occurrences of a value at known offsets -----------------
 *
 * We collect all match positions first, then build the output buffer in
 * a single pass to avoid quadratic copies.
 */
#define MAX_MATCHES 64

static char *replace_in_body(const char *body, size_t body_len,
                              const char *json_path,
                              const char *new_value,
                              size_t     *out_len) {
    /* gather all match positions */
    typedef struct { size_t start; size_t old_len; } match_t;
    match_t matches[MAX_MATCHES];
    int     nmatches = 0;

    size_t  search_from = 0;
    while (search_from < body_len && nmatches < MAX_MATCHES) {
        size_t voff = 0, vlen = 0;
        const char *found = find_value_in_json(body + search_from,
                                                body_len - search_from,
                                                json_path, &voff, &vlen);
        if (!found) break;
        matches[nmatches].start   = search_from + voff;
        matches[nmatches].old_len = vlen;
        nmatches++;
        search_from = search_from + voff + vlen;
    }

    if (!nmatches) return NULL;

    size_t new_val_len = strlen(new_value);
    size_t delta = (size_t)nmatches * new_val_len; /* total new value bytes */
    size_t removed = 0;
    for (int i = 0; i < nmatches; i++) removed += matches[i].old_len;

    size_t out_size = body_len - removed + delta + 1;
    char *out = malloc(out_size);
    if (!out) return NULL;

    size_t wpos = 0, rpos = 0;
    for (int i = 0; i < nmatches; i++) {
        /* copy unchanged bytes before this match */
        size_t prefix = matches[i].start - rpos;
        memcpy(out + wpos, body + rpos, prefix);
        wpos += prefix;
        rpos += prefix;
        /* write new value */
        memcpy(out + wpos, new_value, new_val_len);
        wpos += new_val_len;
        /* skip old value */
        rpos += matches[i].old_len;
    }
    /* copy tail */
    memcpy(out + wpos, body + rpos, body_len - rpos);
    wpos += body_len - rpos;
    out[wpos] = '\0';
    *out_len = wpos;
    return out;
}

/* ---- public API ---------------------------------------------------------- */

void intercept_ctx_init(intercept_ctx_t *ctx) {
    if (ctx) memset(ctx, 0, sizeof(*ctx));
}

uint32_t intercept_add_rule(intercept_ctx_t *ctx,
                             const char *json_path,
                             const char *value_str,
                             const char *url_pattern) {
    if (!ctx || !json_path || !value_str) return 0;
    if (ctx->count >= MAX_RULES) return 0;

    patch_rule_t *r = &ctx->rules[ctx->count++];
    r->id = ++ctx->next_id;
    r->active = 1;
    strncpy(r->json_path,    json_path,    RULE_PATH_LEN - 1);
    strncpy(r->value_str,    value_str,    RULE_VALUE_LEN - 1);
    strncpy(r->url_pattern,  url_pattern ? url_pattern : "*",
            RULE_PATTERN_LEN - 1);

    __android_log_print(ANDROID_LOG_INFO, TAG,
        "rule #%u: [%s] %s = %s",
        r->id, r->url_pattern, r->json_path, r->value_str);
    return r->id;
}

void intercept_remove_rule(intercept_ctx_t *ctx, uint32_t id) {
    if (!ctx) return;
    for (uint32_t i = 0; i < ctx->count; i++) {
        if (ctx->rules[i].id == id) {
            ctx->rules[i].active = 0;
            __android_log_print(ANDROID_LOG_INFO, TAG, "rule #%u removed", id);
            return;
        }
    }
}

char *intercept_apply(intercept_ctx_t *ctx,
                      const char      *url,
                      const char      *body,
                      size_t           body_len,
                      size_t          *out_len) {
    if (!ctx || !body || !body_len) return NULL;

    char   *current = NULL;
    size_t  current_len = body_len;
    /* work on the original first, then accumulate into modified copies */

    for (uint32_t i = 0; i < ctx->count; i++) {
        patch_rule_t *r = &ctx->rules[i];
        if (!r->active) continue;
        if (!glob_match(r->url_pattern, url ? url : "")) continue;

        const char *src     = current ? current : body;
        size_t      src_len = current_len;
        size_t      new_len = 0;

        char *patched = replace_in_body(src, src_len,
                                        r->json_path, r->value_str,
                                        &new_len);
        if (patched) {
            free(current);
            current     = patched;
            current_len = new_len;
            __android_log_print(ANDROID_LOG_DEBUG, TAG,
                "applied rule #%u: %s", r->id, r->json_path);
        }
    }

    if (current) *out_len = current_len;
    return current; /* NULL if nothing was modified */
}
