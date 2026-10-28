/* ── jsmn.h ─────────────────────────────────────────────────────────────────
 * Minimal single-header JSON tokenizer (jsmn v1.1.0 – MIT licence).
 * https://github.com/zserge/jsmn
 *
 * Only the declarations needed for schema_loader.c are exposed here.
 * Full source is inlined in schema_loader.c for simplicity.
 * ─────────────────────────────────────────────────────────────────────────── */

#ifndef JSMN_H
#define JSMN_H

#include <stddef.h>

typedef enum {
    JSMN_UNDEFINED = 0,
    JSMN_OBJECT    = 1,
    JSMN_ARRAY     = 2,
    JSMN_STRING    = 4,
    JSMN_PRIMITIVE = 8
} jsmntype_t;

typedef enum {
    JSMN_ERROR_NOMEM = -1,
    JSMN_ERROR_INVAL = -2,
    JSMN_ERROR_PART  = -3
} jsmnerr_t;

typedef struct {
    jsmntype_t type;
    int start;
    int end;
    int size;
} jsmntok_t;

typedef struct {
    unsigned int pos;
    unsigned int toknext;
    int          toksuper;
} jsmn_parser;

void jsmn_init(jsmn_parser *parser);
int  jsmn_parse(jsmn_parser *parser, const char *js, size_t len,
                jsmntok_t *tokens, unsigned int num_tokens);

#endif /* JSMN_H */
