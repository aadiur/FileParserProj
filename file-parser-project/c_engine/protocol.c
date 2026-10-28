/* ── protocol.c ─────────────────────────────────────────────────────────────
 * Implements the serial wire protocol decoder.
 * See protocol.h for the frame format specification.
 * ──────────────────────────────────────────────────────────────────────── */

#include "protocol.h"
#include "error_handler.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* ── helpers ─────────────────────────────────────────────────────────────── */

static void safe_str(char *dst, size_t dsz, const char *src) {
    if (!src) { dst[0] = '\0'; return; }
    size_t n = strlen(src);
    if (n >= dsz) n = dsz - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/* Split line at '|' delimiters, honouring '\|' escape.
 * Returns number of tokens, writes pointers into toks[].
 * Caller must own/copy the tokens before next call. */
static int split_pipe(char *line, char **toks, int max_toks) {
    int n = 0;
    char *p = line;
    toks[n++] = p;
    while (*p && n < max_toks) {
        if (*p == '\\' && *(p+1)) { p += 2; continue; }
        if (*p == '|') {
            *p = '\0';
            toks[n++] = p + 1;
        }
        p++;
    }
    return n;
}

/* Unescape \| \\ \, \= in-place */
void proto_unescape(char *buf, size_t bufsz) {
    char tmp[MAX_RECORD_LINE];
    size_t i = 0, j = 0;
    while (buf[i] && j + 1 < bufsz) {
        if (buf[i] == '\\' && buf[i+1]) {
            char c = buf[i+1];
            if (c == '|' || c == '\\' || c == ',' || c == '=')
                { tmp[j++] = c; i += 2; continue; }
        }
        tmp[j++] = buf[i++];
    }
    tmp[j] = '\0';
    safe_str(buf, bufsz, tmp);
}

/* Parse "name=value,name=value,..." into Record fields */
static int parse_record_payload(const char *payload, Record *rec) {
    /* work on a mutable copy */
    char buf[MAX_RECORD_LINE];
    safe_str(buf, sizeof(buf), payload);

    rec->field_count = 0;
    char *p = buf;

    while (*p && rec->field_count < MAX_FIELDS) {
        /* find '=' */
        char *eq = NULL;
        char *scan = p;
        while (*scan) {
            if (*scan == '\\' && *(scan+1)) { scan += 2; continue; }
            if (*scan == '=') { eq = scan; break; }
            scan++;
        }
        if (!eq) break; /* malformed pair */

        *eq = '\0';
        char *name_start = p;
        char *val_start  = eq + 1;

        /* find next unescaped ',' */
        char *comma = NULL;
        scan = val_start;
        while (*scan) {
            if (*scan == '\\' && *(scan+1)) { scan += 2; continue; }
            if (*scan == ',') { comma = scan; break; }
            scan++;
        }
        if (comma) { *comma = '\0'; p = comma + 1; }
        else        { p = val_start + strlen(val_start); }

        Field *f = &rec->fields[rec->field_count++];
        proto_unescape(name_start, MAX_FIELD_NAME);
        proto_unescape(val_start,  MAX_FIELD_LEN);
        safe_str(f->name,  sizeof(f->name),  name_start);
        safe_str(f->value, sizeof(f->value),  val_start);
        f->truncated = 0;
    }
    return (int)rec->field_count;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

void proto_init(ProtoContext *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->state    = PROTO_IDLE;
    ctx->next_seq = 0;
    ctx->meta.transfer_complete = 0;
    ctx->meta.transfer_ok       = 0;
}

int proto_feed_line(ProtoContext *ctx, const char *line, Record *rec) {
    /* strip trailing CR/LF */
    char buf[MAX_RECORD_LINE];
    safe_str(buf, sizeof(buf), line);
    size_t len = strlen(buf);
    while (len > 0 && (buf[len-1] == '\r' || buf[len-1] == '\n'))
        buf[--len] = '\0';

    if (len == 0) return 0; /* blank line – ignore */

    /* tokenise on '|' */
    char *toks[8];
    int   ntoks = split_pipe(buf, toks, 8);

    if (ntoks < 3) {
        error_add(0, ERR_PROTOCOL, "", line, "malformed frame: fewer than 3 pipe-separated fields");
        ctx->state = PROTO_ERROR;
        return -1;
    }

    const char *msg_type    = toks[0];
    const char *transfer_id = toks[1];
    const char *seq_str     = toks[2];
    const char *payload     = (ntoks >= 4) ? toks[3] : "";

    /* validate seq */
    char *endp;
    unsigned long seq_ul = strtoul(seq_str, &endp, 10);
    if (*endp != '\0') {
        error_add(0, ERR_PROTOCOL, "SEQ", seq_str, "non-numeric sequence number");
        ctx->state = PROTO_ERROR;
        return -1;
    }
    unsigned int seq = (unsigned int)seq_ul;

    /* ── START ─────────────────────────────────────────────────────────── */
    if (strcmp(msg_type, "START") == 0) {
        if (ctx->state != PROTO_IDLE) {
            error_add(0, ERR_PROTOCOL, "START", "", "START received while already in transfer");
            ctx->state = PROTO_ERROR;
            return -1;
        }
        if (seq != 0) {
            error_add(0, ERR_PROTOCOL, "SEQ", seq_str, "START message must have SEQ=0");
            ctx->state = PROTO_ERROR;
            return -1;
        }
        /* parse payload: filename|format|expected_count  (already split by outer pipe – no) */
        /* payload is everything after the third '|', subfields separated by '|' */
        /* toks[3] = filename, toks[4] = format, toks[5] = expected_count if ntoks>=6 */
        safe_str(ctx->meta.transfer_id, sizeof(ctx->meta.transfer_id), transfer_id);
        if (ntoks >= 5) {
            safe_str(ctx->meta.filename, sizeof(ctx->meta.filename), toks[3]);
            safe_str(ctx->meta.format,   sizeof(ctx->meta.format),   toks[4]);
        }
        if (ntoks >= 6) {
            ctx->meta.expected_records = (unsigned int)strtoul(toks[5], NULL, 10);
        }
        ctx->state    = PROTO_IN_TRANSFER;
        ctx->next_seq = 1;
        return 0;
    }

    /* ── ERR ────────────────────────────────────────────────────────────── */
    if (strcmp(msg_type, "ERR") == 0) {
        safe_str(ctx->meta.error_msg, sizeof(ctx->meta.error_msg), payload);
        error_add(0, ERR_PROTOCOL, "ERR", payload, "Arduino reported transfer error");
        ctx->state = PROTO_ERROR;
        ctx->meta.transfer_complete = 1;
        ctx->meta.transfer_ok       = 0;
        return -1;
    }

    /* For REC and END we must be in a transfer */
    if (ctx->state != PROTO_IN_TRANSFER) {
        error_add(0, ERR_PROTOCOL, msg_type, "", "message outside of transfer (no START seen)");
        ctx->state = PROTO_ERROR;
        return -1;
    }

    /* transfer_id consistency */
    if (strcmp(transfer_id, ctx->meta.transfer_id) != 0) {
        error_add(0, ERR_PROTOCOL, "TRANSFER_ID", transfer_id, "transfer ID mismatch");
        ctx->state = PROTO_ERROR;
        return -1;
    }

    /* ── REC ────────────────────────────────────────────────────────────── */
    if (strcmp(msg_type, "REC") == 0) {
        if (seq < ctx->next_seq) {
            /* duplicate — log and skip */
            error_add(seq, ERR_PROTOCOL, "SEQ", seq_str, "duplicate sequence number – skipped");
            ctx->duplicate_detected = 1;
            return 0;
        }
        if (seq > ctx->next_seq) {
            char reason[128];
            snprintf(reason, sizeof(reason),
                     "out-of-order SEQ: expected %u got %u", ctx->next_seq, seq);
            error_add(seq, ERR_PROTOCOL, "SEQ", seq_str, reason);
            ctx->state = PROTO_ERROR;
            return -1;
        }
        /* seq == next_seq */
        memset(rec, 0, sizeof(*rec));
        rec->record_id   = ctx->next_seq; /* 1-based */
        rec->seq_num     = seq;
        rec->is_anomaly  = -1;
        rec->state       = VALID_UNKNOWN;
        safe_str(rec->raw_line, sizeof(rec->raw_line), line);

        if (parse_record_payload(payload, rec) < 0) {
            error_add(seq, ERR_PROTOCOL, "payload", payload, "failed to parse record fields");
            ctx->state = PROTO_ERROR;
            return -1;
        }
        ctx->next_seq++;
        return 1;
    }

    /* ── END ────────────────────────────────────────────────────────────── */
    if (strcmp(msg_type, "END") == 0) {
        unsigned int sent = (unsigned int)strtoul(payload, NULL, 10);
        ctx->meta.transfer_complete = 1;
        unsigned int actual = ctx->next_seq - 1; /* records received */
        if (sent != actual) {
            char reason[128];
            snprintf(reason, sizeof(reason),
                     "END count mismatch: Arduino says %u, engine received %u", sent, actual);
            error_add(0, ERR_PROTOCOL, "END_COUNT", payload, reason);
            ctx->meta.transfer_ok = 0;
            ctx->state = PROTO_ERROR;
        } else {
            ctx->meta.transfer_ok = 1;
            ctx->state = PROTO_COMPLETE;
        }
        return 0;
    }

    /* unknown message type */
    {
        char reason[128];
        snprintf(reason, sizeof(reason), "unknown message type '%s'", msg_type);
        error_add(0, ERR_PROTOCOL, "MSG_TYPE", msg_type, reason);
        ctx->state = PROTO_ERROR;
        return -1;
    }
}

int proto_parse_file(const char *path,
                     Record *records, int max_records,
                     TransferMeta *meta_out)
{
    FILE *fp = fopen(path, "r");
    if (!fp) {
        char reason[256];
        snprintf(reason, sizeof(reason), "cannot open replay file: %s", path);
        error_add(0, ERR_IO, "file", path, reason);
        return -1;
    }

    ProtoContext ctx;
    proto_init(&ctx);

    int  count    = 0;
    char line[MAX_RECORD_LINE * 2];
    int  had_error = 0;

    while (fgets(line, (int)sizeof(line), fp)) {
        if (count >= max_records) {
            error_add(0, ERR_OVERFLOW, "records", "", "record buffer full; remaining input ignored");
            break;
        }
        int rc = proto_feed_line(&ctx, line, &records[count]);
        if (rc == 1)  { count++;           }
        if (rc == -1) { had_error = 1; break; }
        if (ctx.state == PROTO_COMPLETE) break;
    }

    fclose(fp);

    if (meta_out) *meta_out = ctx.meta;

    if (ctx.state != PROTO_COMPLETE && !had_error) {
        /* reached EOF without END message */
        error_add(0, ERR_PROTOCOL, "END", "",
                  "EOF reached without END message – transfer incomplete");
        ctx.meta.transfer_ok = 0;
        if (meta_out) meta_out->transfer_ok = 0;
    }

    return count;
}
