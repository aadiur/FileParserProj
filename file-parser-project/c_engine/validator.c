/* ── validator.c ─────────────────────────────────────────────────────────── */
#include "validator.h"
#include "error_handler.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>

/* ─── duplicate detection ────────────────────────────────────────────── */
#define DUP_TABLE_SIZE 2048

typedef struct DupEntry {
    char key[256];
    struct DupEntry *next;
} DupEntry;

static DupEntry  dup_pool[MAX_RECORDS];
static DupEntry *dup_table[DUP_TABLE_SIZE];
static int       dup_pool_used = 0;
static int       dup_enabled   = 0;

static unsigned int dup_hash(const char *s) {
    unsigned int h = 5381;
    while (*s) h = ((h << 5) + h) ^ (unsigned char)*s++;
    return h % DUP_TABLE_SIZE;
}

static int dup_seen(const char *key) {
    unsigned int h = dup_hash(key);
    for (DupEntry *e = dup_table[h]; e; e = e->next)
        if (strcmp(e->key, key) == 0) return 1;
    return 0;
}

static void dup_insert(const char *key) {
    if (dup_pool_used >= MAX_RECORDS) return;
    unsigned int h = dup_hash(key);
    DupEntry *e = &dup_pool[dup_pool_used++];
    strncpy(e->key, key, sizeof(e->key)-1);
    e->key[sizeof(e->key)-1] = '\0';
    e->next = dup_table[h];
    dup_table[h] = e;
}

/* ─── helpers ────────────────────────────────────────────────────────── */

static int is_valid_int(const char *s, long *out) {
    if (!s || !*s) return 0;
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (*end != '\0' || errno != 0) return 0;
    if (out) *out = v;
    return 1;
}

static int is_valid_float(const char *s, double *out) {
    if (!s || !*s) return 0;
    char *end;
    errno = 0;
    double v = strtod(s, &end);
    if (*end != '\0' || errno != 0) return 0;
    if (!isfinite(v)) return 0;
    if (out) *out = v;
    return 1;
}

/* find field value by name in record (case-sensitive) */
static const char *field_value(const Record *rec, const char *name) {
    for (unsigned int i = 0; i < rec->field_count; i++)
        if (strcmp(rec->fields[i].name, name) == 0)
            return rec->fields[i].value;
    return NULL;
}

/* ─── Public API ──────────────────────────────────────────────────────── */

void validator_init(const Schema *schema) {
    memset(dup_table, 0, sizeof(dup_table));
    dup_pool_used = 0;
    dup_enabled   = schema->duplicate_detection;
}

void validator_reset(void) {
    memset(dup_table, 0, sizeof(dup_table));
    dup_pool_used = 0;
}

int validate_record(Record *rec, const Schema *schema) {
    int valid = 1;

    /* ── check each schema field ────────────────────────────────────── */
    for (int fi = 0; fi < schema->field_count; fi++) {
        const SchemaField *sf = &schema->fields[fi];
        const char *val = field_value(rec, sf->name);

        if (!val || val[0] == '\0') {
            if (sf->required) {
                char reason[128];
                snprintf(reason, sizeof(reason), "required field '%s' is missing", sf->name);
                error_add(rec->record_id, ERR_MISSING_FIELD, sf->name, "", reason);
                valid = 0;
            }
            continue;
        }

        /* type check */
        if (sf->type == FTYPE_INT) {
            long iv;
            if (!is_valid_int(val, &iv)) {
                char reason[128];
                snprintf(reason, sizeof(reason),
                         "field '%s': '%s' is not a valid integer", sf->name, val);
                error_add(rec->record_id, ERR_TYPE, sf->name, val, reason);
                valid = 0;
                continue;
            }
            double dv = (double)iv;
            if (sf->has_min && dv < sf->min_val) {
                char reason[128];
                snprintf(reason, sizeof(reason),
                         "field '%s': %ld < min (%.4g)", sf->name, iv, sf->min_val);
                error_add(rec->record_id, ERR_RANGE, sf->name, val, reason);
                valid = 0;
            }
            if (sf->has_max && dv > sf->max_val) {
                char reason[128];
                snprintf(reason, sizeof(reason),
                         "field '%s': %ld > max (%.4g)", sf->name, iv, sf->max_val);
                error_add(rec->record_id, ERR_RANGE, sf->name, val, reason);
                valid = 0;
            }
        } else if (sf->type == FTYPE_FLOAT) {
            double fv;
            if (!is_valid_float(val, &fv)) {
                char reason[128];
                snprintf(reason, sizeof(reason),
                         "field '%s': '%s' is not a valid number", sf->name, val);
                error_add(rec->record_id, ERR_TYPE, sf->name, val, reason);
                valid = 0;
                continue;
            }
            if (sf->has_min && fv < sf->min_val) {
                char reason[128];
                snprintf(reason, sizeof(reason),
                         "field '%s': %.4g < min (%.4g)", sf->name, fv, sf->min_val);
                error_add(rec->record_id, ERR_RANGE, sf->name, val, reason);
                valid = 0;
            }
            if (sf->has_max && fv > sf->max_val) {
                char reason[128];
                snprintf(reason, sizeof(reason),
                         "field '%s': %.4g > max (%.4g)", sf->name, fv, sf->max_val);
                error_add(rec->record_id, ERR_RANGE, sf->name, val, reason);
                valid = 0;
            }
        }
        /* FTYPE_STRING: no type-specific check beyond presence */
    }

    /* ── extra field check ────────────────────────────────────────────── */
    for (unsigned int fi = 0; fi < rec->field_count; fi++) {
        const char *fname = rec->fields[fi].name;
        int found = 0;
        for (int si = 0; si < schema->field_count; si++)
            if (strcmp(schema->fields[si].name, fname) == 0) { found = 1; break; }
        if (!found) {
            char reason[128];
            snprintf(reason, sizeof(reason), "extra field '%s' not in schema", fname);
            error_add(rec->record_id, ERR_EXTRA_FIELD, fname,
                      rec->fields[fi].value, reason);
            /* extra fields are non-fatal — log but do not invalidate */
        }
    }

    /* ── duplicate detection ──────────────────────────────────────────── */
    if (dup_enabled && valid) {
        /* build composite key from all required string fields */
        char key[512];
        key[0] = '\0';
        for (int fi = 0; fi < schema->field_count; fi++) {
            if (!schema->fields[fi].required) continue;
            const char *v = field_value(rec, schema->fields[fi].name);
            strncat(key, v ? v : "", sizeof(key) - strlen(key) - 2);
            strncat(key, "|", sizeof(key) - strlen(key) - 1);
        }
        if (dup_seen(key)) {
            char reason[256];
            snprintf(reason, sizeof(reason), "duplicate record (key='%.100s')", key);
            error_add(rec->record_id, ERR_DUPLICATE, "", key, reason);
            valid = 0;
        } else {
            dup_insert(key);
        }
    }

    rec->schema_valid = valid ? 1 : 0;
    rec->state = valid ? VALID_OK : VALID_SCHEMA_FAIL;
    return valid;
}
