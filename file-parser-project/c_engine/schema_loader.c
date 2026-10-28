/* ── schema_loader.c ─────────────────────────────────────────────────────────
 * Parses the user's JSON configuration file using jsmn (embedded below).
 *
 * NOTE: This is the application's own configuration format.  It is NOT a
 * full implementation of the formal JSON Schema specification.
 * ──────────────────────────────────────────────────────────────────────── */

#include "schema_loader.h"
#include "error_handler.h"
#include "jsmn.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>

/* ─── jsmn implementation (embedded) ──────────────────────────────────── */
/* jsmn v1.1.0 — MIT licence — https://github.com/zserge/jsmn            */

static jsmntok_t *jsmn_alloc_token(jsmn_parser *parser,
                                   jsmntok_t *tokens, size_t num_tokens) {
    if (parser->toknext >= num_tokens) return NULL;
    jsmntok_t *tok = &tokens[parser->toknext++];
    tok->start = tok->end = -1;
    tok->size  = 0;
    return tok;
}

static void jsmn_fill_token(jsmntok_t *token, jsmntype_t type, int start, int end) {
    token->type  = type;
    token->start = start;
    token->end   = end;
    token->size  = 0;
}

static int jsmn_parse_primitive(jsmn_parser *parser, const char *js,
                                size_t len, jsmntok_t *tokens, size_t num_tokens) {
    jsmntok_t *token;
    int start = (int)parser->pos;
    for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++) {
        char c = js[parser->pos];
        switch (c) {
        case ':': case ',': case ']': case '}': case '\t':
        case '\r': case '\n': case ' ':
            goto found;
        default: break;
        }
        if (c < 32 || c >= 127) return JSMN_ERROR_INVAL;
    }
found:
    if (!tokens) { parser->pos--; return 0; }
    token = jsmn_alloc_token(parser, tokens, num_tokens);
    if (!token) { parser->pos = (unsigned int)start; return JSMN_ERROR_NOMEM; }
    jsmn_fill_token(token, JSMN_PRIMITIVE, start, (int)parser->pos);
    parser->pos--;
    return 0;
}

static int jsmn_parse_string(jsmn_parser *parser, const char *js,
                             size_t len, jsmntok_t *tokens, size_t num_tokens) {
    jsmntok_t *token;
    int start = (int)parser->pos++;
    for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++) {
        char c = js[parser->pos];
        if (c == '\"') {
            if (!tokens) return 0;
            token = jsmn_alloc_token(parser, tokens, num_tokens);
            if (!token) { parser->pos = (unsigned int)start; return JSMN_ERROR_NOMEM; }
            jsmn_fill_token(token, JSMN_STRING, start + 1, (int)parser->pos);
            return 0;
        }
        if (c == '\\' && parser->pos + 1 < len) {
            parser->pos++;
            switch (js[parser->pos]) {
            case '\"': case '/': case '\\': case 'b':
            case 'f':  case 'r': case 'n':  case 't': break;
            case 'u':
                for (int i = 0; i < 4 && parser->pos < len; i++) {
                    parser->pos++;
                    char h = js[parser->pos];
                    if (!((h >= '0' && h <= '9') || (h >= 'A' && h <= 'F') ||
                          (h >= 'a' && h <= 'f')))
                        return JSMN_ERROR_INVAL;
                }
                break;
            default: return JSMN_ERROR_INVAL;
            }
        }
    }
    return JSMN_ERROR_PART;
}

void jsmn_init(jsmn_parser *parser) {
    parser->pos      = 0;
    parser->toknext  = 0;
    parser->toksuper = -1;
}

int jsmn_parse(jsmn_parser *parser, const char *js, size_t len,
               jsmntok_t *tokens, unsigned int num_tokens) {
    int r;
    int i;
    jsmntok_t *token;
    int count = (int)parser->toknext;

    for (; parser->pos < len && js[parser->pos] != '\0'; parser->pos++) {
        char c = js[parser->pos];
        jsmntype_t type;
        switch (c) {
        case '{': case '[':
            count++;
            if (!tokens) break;
            token = jsmn_alloc_token(parser, tokens, num_tokens);
            if (!token) return JSMN_ERROR_NOMEM;
            if (parser->toksuper != -1) {
                jsmntok_t *t = &tokens[parser->toksuper];
                t->size++;
            }
            token->type  = (c == '{' ? JSMN_OBJECT : JSMN_ARRAY);
            token->start = (int)parser->pos;
            parser->toksuper = (int)(parser->toknext - 1);
            break;
        case '}': case ']':
            if (!tokens) break;
            type = (c == '}' ? JSMN_OBJECT : JSMN_ARRAY);
            for (i = (int)parser->toknext - 1; i >= 0; i--) {
                token = &tokens[i];
                if (token->start != -1 && token->end == -1) {
                    if (token->type != type) return JSMN_ERROR_INVAL;
                    parser->toksuper = -1;
                    token->end = (int)parser->pos + 1;
                    break;
                }
            }
            if (i == -1) return JSMN_ERROR_INVAL;
            for (; i >= 0; i--) {
                token = &tokens[i];
                if (token->start != -1 && token->end == -1) {
                    parser->toksuper = i;
                    break;
                }
            }
            break;
        case '\"':
            r = jsmn_parse_string(parser, js, len, tokens, num_tokens);
            if (r < 0) return r;
            count++;
            if (parser->toksuper != -1 && tokens)
                tokens[parser->toksuper].size++;
            break;
        case '\t': case '\r': case '\n': case ' ':
            break;
        case ':':
            parser->toksuper = (int)parser->toknext - 1;
            break;
        case ',':
            if (tokens && parser->toksuper != -1 &&
                tokens[parser->toksuper].type != JSMN_ARRAY &&
                tokens[parser->toksuper].type != JSMN_OBJECT)
            {
                for (i = (int)parser->toknext - 1; i >= 0; i--) {
                    if (tokens[i].type == JSMN_ARRAY || tokens[i].type == JSMN_OBJECT) {
                        if (tokens[i].start != -1 && tokens[i].end == -1) {
                            parser->toksuper = i;
                            break;
                        }
                    }
                }
            }
            break;
        default:
            r = jsmn_parse_primitive(parser, js, len, tokens, num_tokens);
            if (r < 0) return r;
            count++;
            if (parser->toksuper != -1 && tokens)
                tokens[parser->toksuper].size++;
            break;
        }
    }
    if (tokens) {
        for (i = (int)parser->toknext - 1; i >= 0; i--)
            if (tokens[i].start != -1 && tokens[i].end == -1)
                return JSMN_ERROR_PART;
    }
    return count;
}

/* ─── helpers ─────────────────────────────────────────────────────────── */

static void tok_str(const char *js, const jsmntok_t *tok, char *dst, size_t dsz) {
    size_t n = (size_t)(tok->end - tok->start);
    if (n >= dsz) n = dsz - 1;
    memcpy(dst, js + tok->start, n);
    dst[n] = '\0';
}

static int tok_eq(const char *js, const jsmntok_t *tok, const char *s) {
    size_t slen = strlen(s);
    size_t tlen = (size_t)(tok->end - tok->start);
    return (tlen == slen && memcmp(js + tok->start, s, slen) == 0);
}

/* ─── Public API ──────────────────────────────────────────────────────── */

#define MAX_TOKENS 512

int schema_load(const char *path, Schema *schema_out) {
    memset(schema_out, 0, sizeof(*schema_out));

    /* sensible defaults */
    schema_out->if_contamination = 0.1;
    schema_out->if_n_estimators  = 100;
    schema_out->if_random_state  = 42;
    strcpy(schema_out->txt_delimiter, "|");
    strcpy(schema_out->output_format, "both");
    schema_out->has_header = 1;

    /* read file */
    FILE *fp = fopen(path, "r");
    if (!fp) {
        char msg[256];
        snprintf(msg, sizeof(msg), "cannot open schema file: %s", path);
        error_add(0, ERR_IO, "schema", path, msg);
        return -1;
    }
    fseek(fp, 0, SEEK_END);
    long fsz = ftell(fp);
    rewind(fp);
    if (fsz <= 0 || fsz > 65536) {
        fclose(fp);
        error_add(0, ERR_IO, "schema", path, "schema file too large or empty");
        return -1;
    }
    char *js = (char*)malloc((size_t)fsz + 1);
    if (!js) { fclose(fp); error_add(0, ERR_IO, "schema", path, "out of memory"); return -1; }
    size_t nr = fread(js, 1, (size_t)fsz, fp);
    fclose(fp);
    js[nr] = '\0';

    /* tokenize */
    jsmn_parser parser;
    jsmntok_t   tokens[MAX_TOKENS];
    jsmn_init(&parser);
    int ntok = jsmn_parse(&parser, js, nr, tokens, MAX_TOKENS);
    if (ntok < 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "JSON parse error: %d", ntok);
        error_add(0, ERR_SCHEMA, "schema", path, msg);
        free(js);
        return -1;
    }
    if (tokens[0].type != JSMN_OBJECT) {
        error_add(0, ERR_SCHEMA, "schema", path, "root JSON value must be an object");
        free(js);
        return -1;
    }

    /* walk top-level keys */
    int i = 1;
    while (i < ntok) {
        if (tokens[i].type != JSMN_STRING) { i++; continue; }

        if (tok_eq(js, &tokens[i], "format")) {
            tok_str(js, &tokens[i+1], schema_out->format, sizeof(schema_out->format));
            i += 2;
        } else if (tok_eq(js, &tokens[i], "txt_delimiter")) {
            tok_str(js, &tokens[i+1], schema_out->txt_delimiter, sizeof(schema_out->txt_delimiter));
            i += 2;
        } else if (tok_eq(js, &tokens[i], "has_header")) {
            char v[8]; tok_str(js, &tokens[i+1], v, sizeof(v));
            schema_out->has_header = (strcmp(v,"true")==0 || strcmp(v,"1")==0) ? 1 : 0;
            i += 2;
        } else if (tok_eq(js, &tokens[i], "duplicate_detection")) {
            char v[8]; tok_str(js, &tokens[i+1], v, sizeof(v));
            schema_out->duplicate_detection = (strcmp(v,"true")==0 || strcmp(v,"1")==0) ? 1 : 0;
            i += 2;
        } else if (tok_eq(js, &tokens[i], "output_format")) {
            tok_str(js, &tokens[i+1], schema_out->output_format, sizeof(schema_out->output_format));
            i += 2;
        } else if (tok_eq(js, &tokens[i], "if_contamination")) {
            char v[32]; tok_str(js, &tokens[i+1], v, sizeof(v));
            schema_out->if_contamination = atof(v);
            i += 2;
        } else if (tok_eq(js, &tokens[i], "if_n_estimators")) {
            char v[16]; tok_str(js, &tokens[i+1], v, sizeof(v));
            schema_out->if_n_estimators = atoi(v);
            i += 2;
        } else if (tok_eq(js, &tokens[i], "if_random_state")) {
            char v[16]; tok_str(js, &tokens[i+1], v, sizeof(v));
            schema_out->if_random_state = atoi(v);
            i += 2;
        } else if (tok_eq(js, &tokens[i], "fields")) {
            /* array of field objects */
            if (tokens[i+1].type != JSMN_ARRAY) { i += 2; continue; }
            int arr_size = tokens[i+1].size;
            int j = i + 2;
            schema_out->field_count = 0;
            for (int fi = 0; fi < arr_size && schema_out->field_count < MAX_SCHEMA_FIELDS; fi++) {
                if (tokens[j].type != JSMN_OBJECT) { j++; continue; }
                int obj_size = tokens[j].size;
                j++;
                SchemaField *sf = &schema_out->fields[schema_out->field_count];
                memset(sf, 0, sizeof(*sf));
                sf->type     = FTYPE_STRING;
                sf->required = 1;
                sf->has_min  = 0;
                sf->has_max  = 0;
                for (int ki = 0; ki < obj_size; ki++) {
                    if (tokens[j].type != JSMN_STRING) { j += 2; continue; }
                    if (tok_eq(js, &tokens[j], "name")) {
                        tok_str(js, &tokens[j+1], sf->name, sizeof(sf->name));
                    } else if (tok_eq(js, &tokens[j], "type")) {
                        char v[16]; tok_str(js, &tokens[j+1], v, sizeof(v));
                        if (strcmp(v,"int")==0 || strcmp(v,"integer")==0) sf->type = FTYPE_INT;
                        else if (strcmp(v,"float")==0 || strcmp(v,"number")==0) sf->type = FTYPE_FLOAT;
                        else sf->type = FTYPE_STRING;
                    } else if (tok_eq(js, &tokens[j], "required")) {
                        char v[8]; tok_str(js, &tokens[j+1], v, sizeof(v));
                        sf->required = (strcmp(v,"false")==0 || strcmp(v,"0")==0) ? 0 : 1;
                    } else if (tok_eq(js, &tokens[j], "min")) {
                        char v[32]; tok_str(js, &tokens[j+1], v, sizeof(v));
                        sf->min_val = atof(v); sf->has_min = 1;
                    } else if (tok_eq(js, &tokens[j], "max")) {
                        char v[32]; tok_str(js, &tokens[j+1], v, sizeof(v));
                        sf->max_val = atof(v); sf->has_max = 1;
                    } else if (tok_eq(js, &tokens[j], "numeric_feature")) {
                        char v[8]; tok_str(js, &tokens[j+1], v, sizeof(v));
                        sf->is_numeric_feature = (strcmp(v,"true")==0 || strcmp(v,"1")==0) ? 1 : 0;
                    }
                    j += 2;
                }
                schema_out->field_count++;
            }
            i = j; /* advance past array */

            /* build numeric_features list */
            schema_out->numeric_feature_count = 0;
            for (int fi = 0; fi < schema_out->field_count; fi++) {
                if (schema_out->fields[fi].is_numeric_feature &&
                    schema_out->numeric_feature_count < MAX_NUMERIC_FEAT) {
                    strcpy(schema_out->numeric_features[schema_out->numeric_feature_count++],
                           schema_out->fields[fi].name);
                }
            }
        } else {
            /* skip unknown key+value */
            i++;
            /* skip the value token(s) */
            if (i < ntok) {
                int skip = 1;
                if (tokens[i].type == JSMN_OBJECT || tokens[i].type == JSMN_ARRAY)
                    skip = tokens[i].size * 2 + 1;
                i += skip;
            }
        }
    }

    if (schema_out->field_count == 0) {
        error_add(0, ERR_SCHEMA, "fields", path, "schema has no fields defined");
        free(js);
        return -1;
    }
    if (schema_out->format[0] == '\0') {
        error_add(0, ERR_SCHEMA, "format", path, "schema missing 'format' field (csv or txt)");
        free(js);
        return -1;
    }

    free(js);
    return 0;
}

void schema_print(const Schema *schema) {
    printf("Schema: format=%s  has_header=%d  dup_detect=%d  output=%s\n",
           schema->format, schema->has_header,
           schema->duplicate_detection, schema->output_format);
    printf("  Fields (%d):\n", schema->field_count);
    for (int i = 0; i < schema->field_count; i++) {
        const SchemaField *f = &schema->fields[i];
        printf("    [%d] %-20s  type=%d  required=%d  feature=%d",
               i, f->name, f->type, f->required, f->is_numeric_feature);
        if (f->has_min) printf("  min=%.4g", f->min_val);
        if (f->has_max) printf("  max=%.4g", f->max_val);
        printf("\n");
    }
    printf("  IF: contamination=%.3f  n_estimators=%d  random_state=%d\n",
           schema->if_contamination, schema->if_n_estimators, schema->if_random_state);
}
