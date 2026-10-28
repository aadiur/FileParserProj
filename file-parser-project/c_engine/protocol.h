#ifndef PROTOCOL_H
#define PROTOCOL_H

/* ── protocol.h ─────────────────────────────────────────────────────────────
 *
 * Serial wire protocol between Arduino and PC.
 *
 * FRAME FORMAT
 * ============
 * Every message is a single text line terminated by '\n' (LF).
 * The general form is:
 *
 *   MSG_TYPE|TRANSFER_ID|SEQ|PAYLOAD\n
 *
 * Fields are separated by '|'.  A '|' or '\' inside PAYLOAD fields is
 * escaped as '\|' or '\\' respectively.
 *
 * MESSAGE TYPES
 * =============
 *  START   – transfer start
 *            PAYLOAD = filename|format|expected_count
 *  REC     – one record
 *            PAYLOAD = field1_name=field1_val,field2_name=field2_val,...
 *            ',' and '=' inside values are escaped as '\,' and '\='
 *  END     – transfer complete
 *            PAYLOAD = total_sent (unsigned int as decimal string)
 *  ERR     – transfer-level error from Arduino
 *            PAYLOAD = error_description_text
 *
 * TRANSFER_ID is an alphanumeric token ≤ 31 chars identifying the session.
 * SEQ is a decimal unsigned int (0-based), monotonically increasing per message.
 * For START the SEQ is always 0.
 *
 * ──────────────────────────────────────────────────────────────────────── */

#include <stddef.h>
#include "record.h"

/* Parser state machine */
typedef enum {
    PROTO_IDLE = 0,
    PROTO_IN_TRANSFER,
    PROTO_COMPLETE,
    PROTO_ERROR
} ProtoState;

/* Opaque parser context */
typedef struct {
    ProtoState   state;
    TransferMeta meta;
    unsigned int next_seq;          /* expected sequence number for REC msgs */
    int          duplicate_detected;
} ProtoContext;

/* Initialise a fresh parser context */
void proto_init(ProtoContext *ctx);

/* Feed one raw line (NUL-terminated, no trailing LF required).
 * Returns:
 *   0  – line consumed, record not populated
 *   1  – line consumed AND *rec has been populated (REC message)
 *  -1  – protocol error; ctx->state set to PROTO_ERROR
 */
int proto_feed_line(ProtoContext *ctx, const char *line, Record *rec);

/* Parse all lines from file fp into records[].
 * Returns number of records parsed (≥ 0), or -1 on fatal I/O error.
 * records[] must have capacity max_records.
 * transfer_ok is set 1 if END message was seen with consistent count.
 */
int proto_parse_file(const char *path,
                     Record *records, int max_records,
                     TransferMeta *meta_out);

/* Unescape a protocol field value in-place (handles \| \\ \, \=) */
void proto_unescape(char *buf, size_t bufsz);

#endif /* PROTOCOL_H */
