#include "auto_detect.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <android/log.h>

#define TAG "Phantom/AutoDetect"

/* ── tiny case-insensitive strstr ──────────────────────────────────────── */
static const char *ci_strstr(const char *haystack, const char *needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0) return haystack;
    for (; *haystack; haystack++) {
        if (tolower((unsigned char)*haystack) == tolower((unsigned char)*needle)) {
            size_t i;
            for (i = 1; i < nlen; i++) {
                if (tolower((unsigned char)haystack[i]) !=
                    tolower((unsigned char)needle[i])) break;
            }
            if (i == nlen) return haystack;
        }
    }
    return NULL;
}

/*
 * i check whether a field_entry_t belongs to the requested scan mode.
 * GAME categories are < FIELD_APP_SUBSCRIPTION.
 */
static int category_matches_mode(field_category_t cat, uint32_t mode) {
    int is_game = (cat < FIELD_APP_SUBSCRIPTION);
    if (is_game && (mode & SCAN_MODE_GAMES)) return 1;
    if (!is_game && (mode & SCAN_MODE_APPS))  return 1;
    return 0;
}

/* ── deduplicate hits by json_path ─────────────────────────────────────── */
static field_hit_t *find_existing_hit(scan_result_t *r, const char *path) {
    for (uint32_t i = 0; i < r->count; i++) {
        if (strcmp(r->hits[i].json_path, path) == 0)
            return &r->hits[i];
    }
    return NULL;
}

/*
 * ── Minimal JSON key/value walker ──────────────────────────────────────
 *
 * i don't pull in a full JSON parser — i walk the raw text looking for
 * "key": value pairs. good enough for flat and single-level-nested objects.
 * for deeply nested JSON the path reconstruction is approximate but the
 * key matching is still correct.
 *
 * Walk strategy:
 *   - find a quoted string (the key)
 *   - skip whitespace and ':'
 *   - read the value (string / number / bool / null)
 *   - check key against dictionary
 *   - record a hit if matched
 */

static void scan_object(const char *json, size_t len,
                         const char *url,
                         const char *parent_path,
                         uint32_t mode,
                         scan_result_t *result,
                         int depth);

/* read a JSON string starting at p[0]=='"', return pointer past closing '"' */
static const char *read_string(const char *p, char *out, size_t out_max) {
    p++; /* skip opening " */
    size_t i = 0;
    while (*p && *p != '"' && i < out_max - 1) {
        if (*p == '\\') { p++; } /* skip escape */
        out[i++] = *p++;
    }
    out[i] = '\0';
    if (*p == '"') p++;
    return p;
}

/* read a non-string value (number, bool, null) */
static const char *read_value(const char *p, char *out, size_t out_max,
                               uint8_t *is_num, uint8_t *is_bool) {
    *is_num = 0; *is_bool = 0;
    const char *start = p;
    if (*p == '-' || isdigit((unsigned char)*p)) {
        *is_num = 1;
        while (*p && (isdigit((unsigned char)*p) || *p == '.' || *p == '-' || *p == 'e')) p++;
    } else if (strncmp(p, "true", 4) == 0)  { *is_bool = 1; p += 4; }
    else if (strncmp(p, "false", 5) == 0)   { *is_bool = 1; p += 5; }
    else if (strncmp(p, "null",  4) == 0)   { p += 4; }
    size_t len = (size_t)(p - start);
    if (len >= out_max) len = out_max - 1;
    memcpy(out, start, len);
    out[len] = '\0';
    return p;
}

/* i add a value to seen_values if it's not already there */
static void record_seen_value(field_hit_t *h, const char *val) {
    for (uint8_t i = 0; i < h->seen_count; i++) {
        if (strcmp(h->seen_values[i], val) == 0) return; /* already seen */
    }
    if (h->seen_count < MAX_SEEN) {
        strncpy(h->seen_values[h->seen_count++], val, MAX_VALUE_LEN - 1);
    }
}

static void match_key_and_record(const char *key, const char *value,
                                  const char *json_path, const char *url,
                                  value_type_t vtype,
                                  uint32_t mode, scan_result_t *result) {
    if (result->count >= MAX_HITS) return;

    for (size_t i = 0; i < PHANTOM_FIELD_DICT_LEN; i++) {
        const field_entry_t *e = &PHANTOM_FIELD_DICT[i];
        if (!category_matches_mode(e->category, mode)) continue;
        if (!ci_strstr(key, e->pattern)) continue;

        /* matched — deduplicate or create new hit */
        field_hit_t *existing = find_existing_hit(result, json_path);
        if (existing) {
            existing->hit_count++;
            /* update last seen value and accumulate distinct values */
            strncpy(existing->raw_value, value, MAX_VALUE_LEN - 1);
            record_seen_value(existing, value);
            return;
        }

        field_hit_t *h = &result->hits[result->count++];
        strncpy(h->json_path, json_path, MAX_PATH_LEN - 1);
        strncpy(h->url,       url,       sizeof(h->url) - 1);
        strncpy(h->raw_value, value,     MAX_VALUE_LEN - 1);
        h->category  = e->category;
        h->label     = e->label;
        h->vtype     = vtype;
        h->hit_count = 1;
        h->seen_count = 0;
        record_seen_value(h, value);
        return;
    }
}

static void scan_object(const char *json, size_t len,
                         const char *url,
                         const char *parent_path,
                         uint32_t mode,
                         scan_result_t *result,
                         int depth) {
    if (depth > 8 || result->count >= MAX_HITS) return;

    const char *p   = json;
    const char *end = json + len;

    while (p < end && result->count < MAX_HITS) {
        /* find next '"' (start of a key) */
        while (p < end && *p != '"') p++;
        if (p >= end) break;

        /* read key */
        char key[256] = {0};
        p = read_string(p, key, sizeof(key));

        /* skip whitespace + ':' */
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
        if (p >= end || *p != ':') continue;
        p++; /* skip ':' */
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;

        /* build path */
        char path[MAX_PATH_LEN];
        if (parent_path[0])
            snprintf(path, sizeof(path), "%s.%s", parent_path, key);
        else
            snprintf(path, sizeof(path), "%s", key);

        if (p >= end) break;

        if (*p == '{') {
            /* nested object — recurse */
            scan_object(p + 1, (size_t)(end - p - 1), url, path, mode, result, depth + 1);
            /* skip past the nested object */
            int brace = 1;
            p++;
            while (p < end && brace > 0) {
                if (*p == '{') brace++;
                else if (*p == '}') brace--;
                p++;
            }
        } else if (*p == '[') {
            /* array — scan first element for structure */
            p++;
            while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++;
            if (p < end && *p == '{') {
                char arr_path[MAX_PATH_LEN];
                snprintf(arr_path, sizeof(arr_path), "%s[]", path);
                scan_object(p + 1, (size_t)(end - p - 1), url, arr_path, mode, result, depth + 1);
            }
            /* skip past array */
            int bracket = 1;
            while (p < end && bracket > 0) {
                if (*p == '[') bracket++;
                else if (*p == ']') bracket--;
                p++;
            }
        } else if (*p == '"') {
            /*
             * String value — always record.
             * Games return string enums constantly:
             *   result: "NOT_ENOUGH_GOLD" / "OK"
             *   status: "failed" / "success"
             *   subscription: "free" / "premium" / "trial"
             * The user needs to see what values exist before picking a replacement.
             */
            char val[MAX_VALUE_LEN] = {0};
            p = read_string(p, val, sizeof(val));
            match_key_and_record(key, val, path, url, VTYPE_STRING, mode, result);
        } else {
            /* number / JSON bool / null */
            char val[MAX_VALUE_LEN] = {0};
            uint8_t is_num = 0, is_bool = 0;
            p = read_value(p, val, sizeof(val), &is_num, &is_bool);
            if (is_bool) {
                match_key_and_record(key, val, path, url, VTYPE_BOOL, mode, result);
            } else if (is_num) {
                /* int 0/1 used as boolean is extremely common in game APIs */
                value_type_t vt = (strcmp(val,"0")==0 || strcmp(val,"1")==0)
                                  ? VTYPE_INT_BOOL : VTYPE_NUMBER;
                match_key_and_record(key, val, path, url, vt, mode, result);
            }
        }
    }
}

/* ── public API ─────────────────────────────────────────────────────────── */

void phantom_scan_json(const char *body, size_t len,
                       const char *url,
                       uint32_t mode,
                       scan_result_t *result) {
    if (!body || !len || !result) return;

    /* find the opening '{' — skip HTTP/2 headers if caller passed raw frame */
    const char *start = body;
    while ((size_t)(start - body) < len && *start != '{' && *start != '[') start++;
    if ((size_t)(start - body) >= len) return;

    result->scan_mode = mode;
    scan_object(start + 1, len - (size_t)(start - body) - 1,
                url, "", mode, result, 0);

    __android_log_print(ANDROID_LOG_DEBUG, TAG,
        "scan complete: %u hits from %s", result->count, url);
}

char *phantom_scan_result_to_json(const scan_result_t *result) {
    if (!result || result->count == 0) {
        char *empty = malloc(32);
        if (empty) strcpy(empty, "{\"hits\":[],\"count\":0}");
        return empty;
    }

    /* rough upper bound: each hit is ~512 bytes of JSON */
    size_t cap = 64 + result->count * 512;
    char *out = malloc(cap);
    if (!out) return NULL;

    int pos = 0;
    pos += snprintf(out + pos, cap - pos, "{\"hits\":[");

    static const char *vtype_names[] = {
        "number", "bool", "int_bool", "string", "unknown"
    };

    for (uint32_t i = 0; i < result->count; i++) {
        const field_hit_t *h = &result->hits[i];
        int vt = (int)h->vtype;
        if (vt < 0 || vt > 4) vt = 4;

        /* build seen_values JSON array */
        char seen_buf[MAX_SEEN * (MAX_VALUE_LEN + 4)] = "[";
        int spos = 1;
        for (uint8_t s = 0; s < h->seen_count; s++) {
            spos += snprintf(seen_buf + spos, sizeof(seen_buf) - spos,
                "%s\"%s\"", s > 0 ? "," : "", h->seen_values[s]);
        }
        spos += snprintf(seen_buf + spos, sizeof(seen_buf) - spos, "]");

        pos += snprintf(out + pos, cap - pos,
            "%s{"
            "\"path\":\"%s\","
            "\"url\":\"%s\","
            "\"value\":\"%s\","
            "\"vtype\":\"%s\","
            "\"category\":%d,"
            "\"label\":\"%s\","
            "\"seen\":%s,"
            "\"hits\":%u"
            "}",
            i > 0 ? "," : "",
            h->json_path,
            h->url,
            h->raw_value,
            vtype_names[vt],
            (int)h->category,
            h->label ? h->label : "",
            seen_buf,
            h->hit_count
        );
    }

    pos += snprintf(out + pos, cap - pos,
        "],\"count\":%u}", result->count);
    return out;
}

void phantom_scan_result_reset(scan_result_t *result) {
    if (result) memset(result, 0, sizeof(*result));
}
