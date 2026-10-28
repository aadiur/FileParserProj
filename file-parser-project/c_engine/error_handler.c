/* ── error_handler.c ────────────────────────────────────────────────────── */
#include "error_handler.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static ErrorEntry g_errors[MAX_ERRORS];
static unsigned int g_count = 0;

/* Safe bounded string copy */
static void safe_copy(char *dst, size_t dsz, const char *src) {
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlen(src);
    if (n >= dsz) n = dsz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

void error_handler_init(void) {
    memset(g_errors, 0, sizeof(g_errors));
    g_count = 0;
}

void error_add(unsigned int record_id,
               ErrorCategory category,
               const char *field_name,
               const char *original_value,
               const char *reason)
{
    if (g_count >= MAX_ERRORS) {
        fprintf(stderr, "[error_handler] error table full, dropping error for record %u\n",
                record_id);
        return;
    }
    ErrorEntry *e = &g_errors[g_count++];
    e->record_id = record_id;
    e->category  = category;
    safe_copy(e->field_name,      sizeof(e->field_name),      field_name      ? field_name      : "");
    safe_copy(e->original_value,  sizeof(e->original_value),  original_value  ? original_value  : "");
    safe_copy(e->reason,          sizeof(e->reason),           reason          ? reason          : "");
}

unsigned int error_count(void) { return g_count; }

const ErrorEntry *error_get_all(void) { return g_errors; }

void error_clear(void) {
    memset(g_errors, 0, sizeof(g_errors));
    g_count = 0;
}
