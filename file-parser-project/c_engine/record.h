#ifndef RECORD_H
#define RECORD_H

/* ── record.h ──────────────────────────────────────────────────────────────
 * Shared data structures for the C processing engine.
 * All buffer sizes are checked at parse time; values that exceed limits are
 * truncated with an error logged rather than silently overflowing.
 * ──────────────────────────────────────────────────────────────────────── */

#define MAX_FIELDS       20
#define MAX_FIELD_LEN   128
#define MAX_FIELD_NAME   64
#define MAX_RECORDS    2048
#define MAX_ERRORS     4096
#define MAX_RECORD_LINE 512   /* raw wire line limit */

/* ── Field ─────────────────────────────────────────────────────────────── */
typedef struct {
    char name [MAX_FIELD_NAME];
    char value[MAX_FIELD_LEN];
    int  truncated;   /* 1 if value was clipped due to length limit */
} Field;

/* ── Anomaly result from ML module ─────────────────────────────────────── */
typedef struct {
    unsigned int record_id;
    int          is_anomaly;    /* 1 = anomaly, 0 = normal, -1 = not set   */
    double       anomaly_score; /* higher → more anomalous (neg score_samples) */
    int          available;     /* 1 if ML produced a result for this record */
} AnomalyResult;

/* ── Validation state ───────────────────────────────────────────────────── */
typedef enum {
    VALID_UNKNOWN = 0,
    VALID_OK,
    VALID_SCHEMA_FAIL,
    VALID_PROTO_FAIL
} ValidState;

/* ── Core record ────────────────────────────────────────────────────────── */
typedef struct {
    unsigned int record_id;      /* 1-based, assigned by protocol layer     */
    unsigned int seq_num;        /* sequence number from wire message        */
    unsigned int field_count;
    Field        fields[MAX_FIELDS];
    ValidState   state;
    int          schema_valid;   /* 1 = passed schema validation            */
    int          is_anomaly;     /* from ML; -1 = unknown                   */
    double       anomaly_score;
    char         raw_line[MAX_RECORD_LINE]; /* original wire line           */
} Record;

/* ── Error entry ────────────────────────────────────────────────────────── */
typedef enum {
    ERR_NONE = 0,
    ERR_PROTOCOL,
    ERR_SCHEMA,
    ERR_TYPE,
    ERR_RANGE,
    ERR_MISSING_FIELD,
    ERR_EXTRA_FIELD,
    ERR_DUPLICATE,
    ERR_OVERFLOW,
    ERR_IO,
    ERR_ML
} ErrorCategory;

typedef struct {
    unsigned int  record_id;
    ErrorCategory category;
    char          field_name[MAX_FIELD_NAME];
    char          original_value[MAX_FIELD_LEN];
    char          reason[256];
} ErrorEntry;

/* ── Statistics ─────────────────────────────────────────────────────────── */
typedef struct {
    unsigned int total_records;
    unsigned int valid_records;
    unsigned int invalid_records;
    unsigned int anomaly_count;
    unsigned int ml_eligible;
    /* per configured numeric field — only first numeric field tracked here */
    double       num_min;
    double       num_max;
    double       num_sum;
    unsigned int num_count;
    double       num_avg;
    int          has_numeric;
    int          ml_available;  /* 1 = ML ran successfully */
    char         ml_status[128];
} Statistics;

/* ── Transfer metadata from protocol ───────────────────────────────────── */
typedef struct {
    char         transfer_id[32];
    char         filename[128];
    char         format[16];    /* "csv" or "txt" */
    unsigned int expected_records;
    int          transfer_complete;
    int          transfer_ok;
    char         error_msg[256];
} TransferMeta;

#endif /* RECORD_H */
