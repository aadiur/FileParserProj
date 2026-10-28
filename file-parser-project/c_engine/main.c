/* ── main.c ──────────────────────────────────────────────────────────────
 * C processing engine entry point.
 *
 * Usage:
 *   c_engine [OPTIONS]
 *
 * Options:
 *   --input   <path>    Path to replay/serial log file (protocol format)
 *   --schema  <path>    Path to JSON schema configuration
 *   --output  <dir>     Output directory (default: ../output)
 *   --ml-results <path> Path to ML results CSV produced by anomaly_detector.py
 *   --format  <fmt>     Override format: csv or txt
 *   --raw     <path>    Process a raw CSV/TXT file directly (local mode,
 *                        bypasses protocol layer — Python controller writes
 *                        a synthetic protocol log for replay use instead)
 *
 * Exit codes:
 *   0  – success
 *   1  – usage / argument error
 *   2  – schema load failure
 *   3  – input/protocol failure
 *   4  – output write failure
 *   5  – partial success (some records invalid; output written)
 * ──────────────────────────────────────────────────────────────────────── */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "record.h"
#include "protocol.h"
#include "schema_loader.h"
#include "validator.h"
#include "statistics.h"
#include "report_writer.h"
#include "error_handler.h"

/* ── Static record store (avoids heap fragmentation on small platforms) ── */
static Record    g_records[MAX_RECORDS];
static int       g_record_count = 0;

/* ── ML results loading ────────────────────────────────────────────────── */
/*
 * The ML results CSV has the header:
 *   record_id,anomaly_flag,anomaly_score
 * We match by record_id.
 */
static int load_ml_results(const char *path, Record *records, int record_count,
                            Statistics *stats) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        char msg[256];
        snprintf(msg, sizeof(msg), "cannot open ML results file: %s", path);
        error_add(0, ERR_ML, "ml_results", path, msg);
        return -1;
    }

    char line[512];
    /* skip header */
    if (!fgets(line, sizeof(line), fp)) { fclose(fp); return -1; }

    int matched = 0;
    while (fgets(line, sizeof(line), fp)) {
        /* strip newline */
        size_t n = strlen(line);
        while (n > 0 && (line[n-1] == '\r' || line[n-1] == '\n')) line[--n] = '\0';

        unsigned int rid;
        int          aflag;
        double       ascore;
        if (sscanf(line, "%u,%d,%lf", &rid, &aflag, &ascore) != 3) continue;

        /* find matching record */
        for (int i = 0; i < record_count; i++) {
            if (records[i].record_id == rid) {
                records[i].is_anomaly    = aflag;
                records[i].anomaly_score = ascore;
                if (aflag == 1) stats->anomaly_count++;
                matched++;
                break;
            }
        }
    }
    fclose(fp);

    stats->ml_available = 1;
    snprintf(stats->ml_status, sizeof(stats->ml_status),
             "OK – Isolation Forest results loaded (%d matched)", matched);
    return matched;
}

/* ── Synthetic protocol log builder (for --raw mode) ────────────────── */
/*
 * When running in raw/local mode the Python controller converts the CSV/TXT
 * into a protocol replay log.  However main.c also supports a --raw flag so
 * the C engine can directly ingest a CSV and emit the protocol log itself
 * (useful for testing without Python).
 *
 * Protocol escaping: replace '|' → '\|', '\\' → '\\'
 *                    value pairs use ',' separator, ',' → '\,' and '=' → '\='
 */
static void proto_escape_val(const char *in, char *out, size_t outsz) {
    size_t j = 0;
    for (const char *p = in; *p && j + 2 < outsz; p++) {
        char c = *p;
        if (c == '\\' || c == '|' || c == ',' || c == '=') {
            out[j++] = '\\';
        }
        out[j++] = c;
    }
    out[j] = '\0';
}

static int build_protocol_log_from_csv(const char *csv_path,
                                        const Schema *schema,
                                        const char *log_path) {
    FILE *in  = fopen(csv_path, "r");
    FILE *out = fopen(log_path, "w");
    if (!in || !out) {
        if (in)  fclose(in);
        if (out) fclose(out);
        return -1;
    }

    char line[MAX_RECORD_LINE * 2];
    int  total_recs  = 0;
    int  first       = 1;
    char field_names[MAX_FIELDS][MAX_FIELD_NAME];
    int  field_count = 0;

    /* Pass 1: extract headers / count records */
    while (fgets(line, sizeof(line), in)) {
        size_t n = strlen(line);
        while (n > 0 && (line[n-1] == '\r' || line[n-1] == '\n')) line[--n] = '\0';
        if (n == 0) continue;

        if (first && schema->has_header) {
            char tmp[MAX_RECORD_LINE * 2];
            strncpy(tmp, line, sizeof(tmp)-1);
            tmp[sizeof(tmp)-1] = '\0';
            char *tok = strtok(tmp, ",");
            while (tok && field_count < MAX_FIELDS) {
                /* strip quotes if present */
                if (*tok == '"' && tok[strlen(tok)-1] == '"' && strlen(tok) >= 2) {
                    tok[strlen(tok)-1] = '\0';
                    tok++;
                }
                strncpy(field_names[field_count++], tok, MAX_FIELD_NAME-1);
                tok = strtok(NULL, ",");
            }
            first = 0;
            continue;
        }
        if (first) {
            field_count = schema->field_count;
            for (int i = 0; i < field_count; i++)
                strncpy(field_names[i], schema->fields[i].name, MAX_FIELD_NAME-1);
            first = 0;
        }
        total_recs++;
        if (total_recs >= MAX_RECORDS) break;
    }

    /* Write START */
    const char *basename = csv_path;
    for (const char *p = csv_path; *p; p++) if (*p == '/' || *p == '\\') basename = p + 1;
    fprintf(out, "START|TRF001|0|%s|%s|%d\n",
            basename,
            schema->format[0] ? schema->format : "csv",
            total_recs);

    /* Pass 2: rewind and emit REC lines */
    fseek(in, 0, SEEK_SET);
    first = 1;
    int rec_seq = 1;
    int emitted = 0;

    while (fgets(line, sizeof(line), in) && emitted < total_recs) {
        size_t n = strlen(line);
        while (n > 0 && (line[n-1] == '\r' || line[n-1] == '\n')) line[--n] = '\0';
        if (n == 0) continue;
        if (first && schema->has_header) {
            first = 0;
            continue;
        }
        first = 0;

        char *vals[MAX_FIELDS];
        int   vc = 0;
        char  tmp[MAX_RECORD_LINE * 2];
        strncpy(tmp, line, sizeof(tmp)-1);
        tmp[sizeof(tmp)-1] = '\0';

        char *p = tmp;
        while (*p && vc < MAX_FIELDS) {
            char vbuf[MAX_FIELD_LEN * 2];
            int  vpos = 0;
            if (*p == '"') {
                p++;
                while (*p && vpos < (int)sizeof(vbuf)-1) {
                    if (*p == '"' && *(p+1) == '"') { vbuf[vpos++] = '"'; p += 2; }
                    else if (*p == '"') { p++; break; }
                    else vbuf[vpos++] = *p++;
                }
                if (*p == ',') p++;
            } else {
                while (*p && *p != ',' && vpos < (int)sizeof(vbuf)-1)
                    vbuf[vpos++] = *p++;
                if (*p == ',') p++;
            }
            vbuf[vpos] = '\0';
            vals[vc] = (char*)malloc(vpos + 1);
            if (vals[vc]) { memcpy(vals[vc], vbuf, vpos+1); vc++; }
        }

        char payload[MAX_RECORD_LINE * 2];
        int  ppos = 0;
        for (int i = 0; i < vc && ppos < (int)sizeof(payload)-4; i++) {
            char ename[MAX_FIELD_NAME * 2], eval[MAX_FIELD_LEN * 2];
            proto_escape_val((i < field_count) ? field_names[i] : "extra", ename, sizeof(ename));
            proto_escape_val(vals[i] ? vals[i] : "", eval, sizeof(eval));
            if (i > 0) payload[ppos++] = ',';
            ppos += snprintf(payload + ppos, sizeof(payload) - (size_t)ppos,
                             "%s=%s", ename, eval);
        }
        payload[ppos] = '\0';

        fprintf(out, "REC|TRF001|%d|%s\n", rec_seq++, payload);
        emitted++;

        for (int i = 0; i < vc; i++) if (vals[i]) free(vals[i]);
    }
    fclose(in);

    /* Write END */
    fprintf(out, "END|TRF001|%d|%d\n", rec_seq, emitted);
    fclose(out);
    return 0;
}

/* ── TXT format builder ────────────────────────────────────────────────── */
static int build_protocol_log_from_txt(const char *txt_path,
                                        const Schema *schema,
                                        const char *log_path) {
    FILE *in  = fopen(txt_path, "r");
    FILE *out = fopen(log_path, "w");
    if (!in || !out) {
        if (in)  fclose(in);
        if (out) fclose(out);
        return -1;
    }

    const char *delim = schema->txt_delimiter[0] ? schema->txt_delimiter : "|";
    char line[MAX_RECORD_LINE * 2];
    int  total_recs = 0;
    int  first      = 1;

    /* Pass 1: count records */
    while (fgets(line, sizeof(line), in)) {
        size_t n = strlen(line);
        while (n > 0 && (line[n-1] == '\r' || line[n-1] == '\n')) line[--n] = '\0';
        if (n == 0 || line[0] == '#') continue;
        if (first && schema->has_header) { first = 0; continue; }
        first = 0;
        total_recs++;
        if (total_recs >= MAX_RECORDS) break;
    }

    /* Write START */
    const char *basename = txt_path;
    for (const char *p = txt_path; *p; p++) if (*p == '/' || *p == '\\') basename = p + 1;
    fprintf(out, "START|TRF001|0|%s|txt|%d\n", basename, total_recs);

    /* Pass 2: rewind and emit REC */
    fseek(in, 0, SEEK_SET);
    first = 1;
    int rec_seq = 1;
    int emitted = 0;

    while (fgets(line, sizeof(line), in) && emitted < total_recs) {
        size_t n = strlen(line);
        while (n > 0 && (line[n-1] == '\r' || line[n-1] == '\n')) line[--n] = '\0';
        if (n == 0 || line[0] == '#') continue;
        if (first && schema->has_header) { first = 0; continue; }
        first = 0;

        char tmp[MAX_RECORD_LINE * 2];
        strncpy(tmp, line, sizeof(tmp)-1);
        tmp[sizeof(tmp)-1] = '\0';

        char payload[MAX_RECORD_LINE * 2];
        int  ppos = 0;
        int  fi   = 0;
        char *tok = strtok(tmp, delim);
        while (tok && fi < schema->field_count && ppos < (int)sizeof(payload)-4) {
            char ename[MAX_FIELD_NAME * 2], eval[MAX_FIELD_LEN * 2];
            proto_escape_val(schema->fields[fi].name, ename, sizeof(ename));
            proto_escape_val(tok, eval, sizeof(eval));
            if (fi > 0) payload[ppos++] = ',';
            ppos += snprintf(payload + ppos, sizeof(payload) - (size_t)ppos,
                             "%s=%s", ename, eval);
            fi++;
            tok = strtok(NULL, delim);
        }
        payload[ppos] = '\0';

        fprintf(out, "REC|TRF001|%d|%s\n", rec_seq++, payload);
        emitted++;
    }
    fclose(in);

    fprintf(out, "END|TRF001|%d|%d\n", rec_seq, emitted);
    fclose(out);
    return 0;
}

/* ── main ──────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    const char *input_path   = NULL;
    const char *schema_path  = NULL;
    const char *output_dir   = "../output";
    const char *ml_path      = NULL;
    const char *raw_path     = NULL;
    const char *fmt_override = NULL;

    /* ── parse arguments ─────────────────────────────────────────────── */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--input")      == 0 && i+1 < argc) input_path   = argv[++i];
        else if (strcmp(argv[i], "--schema") == 0 && i+1 < argc) schema_path  = argv[++i];
        else if (strcmp(argv[i], "--output") == 0 && i+1 < argc) output_dir   = argv[++i];
        else if (strcmp(argv[i], "--ml-results") == 0 && i+1 < argc) ml_path  = argv[++i];
        else if (strcmp(argv[i], "--raw")   == 0 && i+1 < argc) raw_path     = argv[++i];
        else if (strcmp(argv[i], "--format") == 0 && i+1 < argc) fmt_override = argv[++i];
        else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            return 1;
        }
    }

    if (!schema_path) {
        fprintf(stderr, "Usage: c_engine --schema <path> [--input <proto_log>] "
                        "[--raw <csv_or_txt>] [--output <dir>] "
                        "[--ml-results <path>] [--format csv|txt]\n");
        return 1;
    }
    if (!input_path && !raw_path) {
        fprintf(stderr, "Provide --input <protocol_log> or --raw <csv/txt>\n");
        return 1;
    }

    /* ── init ─────────────────────────────────────────────────────────── */
    error_handler_init();

    /* ── load schema ──────────────────────────────────────────────────── */
    Schema schema;
    if (schema_load(schema_path, &schema) != 0) {
        fprintf(stderr, "[c_engine] Schema load failed.\n");
        return 2;
    }
    if (fmt_override && fmt_override[0]) {
        strncpy(schema.format, fmt_override, sizeof(schema.format)-1);
    }

    /* ── build protocol log from raw file if needed ───────────────────── */
    char tmp_log_path[512];
    const char *proto_input = input_path;

    if (raw_path) {
        /* write temp protocol log alongside the output */
        snprintf(tmp_log_path, sizeof(tmp_log_path), "%s/_tmp_proto.log", output_dir);

        int rc;
        if (strcmp(schema.format, "txt") == 0)
            rc = build_protocol_log_from_txt(raw_path, &schema, tmp_log_path);
        else
            rc = build_protocol_log_from_csv(raw_path, &schema, tmp_log_path);

        if (rc != 0) {
            fprintf(stderr, "[c_engine] Failed to build protocol log from raw file.\n");
            return 3;
        }
        proto_input = tmp_log_path;
    }

    /* ── parse protocol ───────────────────────────────────────────────── */
    TransferMeta meta;
    g_record_count = proto_parse_file(proto_input, g_records, MAX_RECORDS, &meta);
    if (g_record_count < 0) {
        fprintf(stderr, "[c_engine] Protocol parse failed.\n");
        return 3;
    }

    printf("[c_engine] Protocol: %d records received, transfer_ok=%d\n",
           g_record_count, meta.transfer_ok);

    /* ── validate ─────────────────────────────────────────────────────── */
    validator_init(&schema);
    for (int i = 0; i < g_record_count; i++)
        validate_record(&g_records[i], &schema);

    /* ── statistics pass 1 (before ML) ───────────────────────────────── */
    Statistics stats;
    statistics_init(&stats);
    for (int i = 0; i < g_record_count; i++)
        statistics_add_record(&stats, &g_records[i], &schema);

    /* count ML-eligible (schema-valid, has at least one numeric feature) */
    int eligible = 0;
    for (int i = 0; i < g_record_count; i++)
        if (g_records[i].schema_valid) eligible++;
    stats.ml_eligible = (unsigned int)eligible;

    /* ── load ML results if provided ─────────────────────────────────── */
    if (ml_path) {
        int matched = load_ml_results(ml_path, g_records, g_record_count, &stats);
        if (matched < 0) {
            snprintf(stats.ml_status, sizeof(stats.ml_status),
                     "FAILED – could not read ML results file");
            stats.ml_available = 0;
        }
        /* recount anomalies */
        stats.anomaly_count = 0;
        for (int i = 0; i < g_record_count; i++)
            if (g_records[i].schema_valid && g_records[i].is_anomaly == 1)
                stats.anomaly_count++;
    } else {
        strcpy(stats.ml_status, "not requested (no --ml-results)");
    }

    statistics_finalise(&stats);
    statistics_print(&stats);

    /* ── write outputs ────────────────────────────────────────────────── */
    int write_csv = 1;
    int write_txt = (strcmp(schema.output_format, "txt")  == 0 ||
                     strcmp(schema.output_format, "both") == 0);

    /* determine effective input filename for report */
    const char *report_fname = raw_path ? raw_path : (input_path ? input_path : "(unknown)");

    ReportContext rctx = {
        .output_dir       = output_dir,
        .input_filename   = report_fname,
        .schema_filename  = schema_path,
        .format           = schema.format,
        .records          = g_records,
        .record_count     = g_record_count,
        .schema           = &schema,
        .stats            = &stats,
        .transfer_meta    = &meta,
        .write_parsed_csv = write_csv,
        .write_parsed_txt = write_txt,
    };

    if (report_write_all(&rctx) != 0) {
        fprintf(stderr, "[c_engine] Output write error.\n");
        return 4;
    }

    printf("[c_engine] Output written to: %s\n", output_dir);

    /* ── exit code ────────────────────────────────────────────────────── */
    if (stats.invalid_records > 0 || !meta.transfer_ok)
        return 5; /* partial success */
    return 0;
}
