#ifndef SCHEMA_LOADER_H
#define SCHEMA_LOADER_H

/* ── schema_loader.h ─────────────────────────────────────────────────────── */

#define MAX_SCHEMA_FIELDS   20
#define MAX_NUMERIC_FEAT    10

typedef enum {
    FTYPE_STRING = 0,
    FTYPE_INT,
    FTYPE_FLOAT
} FieldType;

typedef struct {
    char      name[64];
    FieldType type;
    int       required;
    int       has_min;
    int       has_max;
    double    min_val;
    double    max_val;
    int       is_numeric_feature; /* used for ML / statistics */
} SchemaField;

typedef struct {
    char        format[16];          /* "csv" or "txt" */
    char        txt_delimiter[8];    /* delimiter for TXT format */
    int         has_header;          /* CSV: first line is header */
    SchemaField fields[MAX_SCHEMA_FIELDS];
    int         field_count;
    int         duplicate_detection; /* 1 = enable */
    char        output_format[16];   /* "csv", "txt", or "both" */
    /* Isolation Forest settings */
    double      if_contamination;    /* 0.0 = "auto" sentinel */
    int         if_n_estimators;
    int         if_random_state;
    /* numeric feature names for ML (subset of schema fields) */
    char        numeric_features[MAX_NUMERIC_FEAT][64];
    int         numeric_feature_count;
} Schema;

/* Load and parse the JSON schema file at path.
 * Returns 0 on success, -1 on failure (errors logged via error_handler). */
int schema_load(const char *path, Schema *schema_out);

/* Print schema summary to stdout (for debugging) */
void schema_print(const Schema *schema);

#endif /* SCHEMA_LOADER_H */
