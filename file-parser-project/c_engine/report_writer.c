/* ── report_writer.c ─────────────────────────────────────────────────────
 * Writes PARSED.CSV, PARSED.TXT, ERRORS.CSV, ANOMALIES.CSV, REPORT.TXT
 * ──────────────────────────────────────────────────────────────────────── */

#include "report_writer.h"
#include "error_handler.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

/* ─── CSV escaping ───────────────────────────────────────────────────── */
void csv_escape(const char *in, char *out, size_t outsz) {
    int needs_quote = 0;
    for (const char *p = in; *p; p++) {
        if (*p == '"' || *p == ',' || *p == '\n' || *p == '\r')
            { needs_quote = 1; break; }
    }
    if (!needs_quote) {
        size_t n = strlen(in);
        if (n >= outsz) n = outsz - 1;
        memcpy(out, in, n); out[n] = '\0';
        return;
    }
    size_t pos = 0;
    if (pos < outsz - 1) out[pos++] = '"';
    for (const char *p = in; *p && pos < outsz - 2; p++) {
        if (*p == '"' && pos < outsz - 3) out[pos++] = '"'; /* double quote */
        out[pos++] = *p;
    }
    if (pos < outsz - 1) out[pos++] = '"';
    out[pos] = '\0';
}

/* ─── path builder ───────────────────────────────────────────────────── */
static void build_path(char *out, size_t outsz, const char *dir, const char *fname) {
    snprintf(out, outsz, "%s/%s", dir, fname);
}

/* ─── PARSED.CSV ─────────────────────────────────────────────────────── */
static int write_parsed_csv(const ReportContext *ctx) {
    char path[512];
    build_path(path, sizeof(path), ctx->output_dir, "PARSED.CSV");
    FILE *fp = fopen(path, "w");
    if (!fp) { perror(path); return -1; }

    /* header */
    fprintf(fp, "record_id");
    for (int fi = 0; fi < ctx->schema->field_count; fi++)
        fprintf(fp, ",%s", ctx->schema->fields[fi].name);
    fprintf(fp, ",anomaly_flag,anomaly_score\n");

    char esc[MAX_FIELD_LEN * 2];
    for (int i = 0; i < ctx->record_count; i++) {
        const Record *r = &ctx->records[i];
        if (!r->schema_valid) continue;

        fprintf(fp, "%u", r->record_id);
        for (int fi = 0; fi < ctx->schema->field_count; fi++) {
            const char *v = "";
            for (unsigned int rf = 0; rf < r->field_count; rf++)
                if (strcmp(r->fields[rf].name, ctx->schema->fields[fi].name) == 0)
                    { v = r->fields[rf].value; break; }
            csv_escape(v, esc, sizeof(esc));
            fprintf(fp, ",%s", esc);
        }
        /* anomaly columns */
        if (r->is_anomaly == -1) {
            fprintf(fp, ",N/A,N/A\n");
        } else {
            fprintf(fp, ",%d,%.6f\n", r->is_anomaly, r->anomaly_score);
        }
    }
    fclose(fp);
    return 0;
}

/* ─── PARSED.TXT ─────────────────────────────────────────────────────── */
static int write_parsed_txt(const ReportContext *ctx) {
    char path[512];
    build_path(path, sizeof(path), ctx->output_dir, "PARSED.TXT");
    FILE *fp = fopen(path, "w");
    if (!fp) { perror(path); return -1; }

    fprintf(fp, "# PARSED.TXT – schema-valid records\n");
    fprintf(fp, "# Format: record_id | field1=value1 | field2=value2 | ... | anomaly_flag | anomaly_score\n\n");

    for (int i = 0; i < ctx->record_count; i++) {
        const Record *r = &ctx->records[i];
        if (!r->schema_valid) continue;

        fprintf(fp, "record_id=%u", r->record_id);
        for (unsigned int fi = 0; fi < r->field_count; fi++)
            fprintf(fp, " | %s=%s", r->fields[fi].name, r->fields[fi].value);
        if (r->is_anomaly == -1)
            fprintf(fp, " | anomaly=N/A | score=N/A\n");
        else
            fprintf(fp, " | anomaly=%d | score=%.6f\n", r->is_anomaly, r->anomaly_score);
    }
    fclose(fp);
    return 0;
}

/* ─── ERRORS.CSV ─────────────────────────────────────────────────────── */
static const char *category_name(ErrorCategory c) {
    switch (c) {
    case ERR_PROTOCOL:     return "PROTOCOL";
    case ERR_SCHEMA:       return "SCHEMA";
    case ERR_TYPE:         return "TYPE";
    case ERR_RANGE:        return "RANGE";
    case ERR_MISSING_FIELD:return "MISSING_FIELD";
    case ERR_EXTRA_FIELD:  return "EXTRA_FIELD";
    case ERR_DUPLICATE:    return "DUPLICATE";
    case ERR_OVERFLOW:     return "OVERFLOW";
    case ERR_IO:           return "IO";
    case ERR_ML:           return "ML";
    default:               return "UNKNOWN";
    }
}

static int write_errors_csv(const ReportContext *ctx) {
    char path[512];
    build_path(path, sizeof(path), ctx->output_dir, "ERRORS.CSV");
    FILE *fp = fopen(path, "w");
    if (!fp) { perror(path); return -1; }

    fprintf(fp, "record_id,category,field_name,original_value,reason\n");

    char esc1[MAX_FIELD_LEN*2], esc2[MAX_FIELD_LEN*2], esc3[512];
    unsigned int ec = error_count();
    const ErrorEntry *errs = error_get_all();
    for (unsigned int i = 0; i < ec; i++) {
        csv_escape(errs[i].field_name,     esc1, sizeof(esc1));
        csv_escape(errs[i].original_value, esc2, sizeof(esc2));
        csv_escape(errs[i].reason,         esc3, sizeof(esc3));
        fprintf(fp, "%u,%s,%s,%s,%s\n",
                errs[i].record_id,
                category_name(errs[i].category),
                esc1, esc2, esc3);
    }
    fclose(fp);
    return 0;
}

/* ─── ANOMALIES.CSV ──────────────────────────────────────────────────── */
static int write_anomalies_csv(const ReportContext *ctx) {
    char path[512];
    build_path(path, sizeof(path), ctx->output_dir, "ANOMALIES.CSV");
    FILE *fp = fopen(path, "w");
    if (!fp) { perror(path); return -1; }

    /* header always present */
    fprintf(fp, "record_id,anomaly_flag,anomaly_score,status");
    for (int fi = 0; fi < ctx->schema->field_count; fi++)
        fprintf(fp, ",%s", ctx->schema->fields[fi].name);
    fprintf(fp, "\n");

    if (!ctx->stats->ml_available) {
        /* ML was unavailable — file has headers but no data rows */
        fclose(fp);
        return 0;
    }

    char esc[MAX_FIELD_LEN * 2];
    for (int i = 0; i < ctx->record_count; i++) {
        const Record *r = &ctx->records[i];
        if (!r->schema_valid) continue;
        if (r->is_anomaly != 1) continue;

        fprintf(fp, "%u,%d,%.6f,anomaly_flagged_for_review",
                r->record_id, r->is_anomaly, r->anomaly_score);
        for (int fi = 0; fi < ctx->schema->field_count; fi++) {
            const char *v = "";
            for (unsigned int rf = 0; rf < r->field_count; rf++)
                if (strcmp(r->fields[rf].name, ctx->schema->fields[fi].name) == 0)
                    { v = r->fields[rf].value; break; }
            csv_escape(v, esc, sizeof(esc));
            fprintf(fp, ",%s", esc);
        }
        fprintf(fp, "\n");
    }
    fclose(fp);
    return 0;
}

/* ─── REPORT.TXT ─────────────────────────────────────────────────────── */
static int write_report_txt(const ReportContext *ctx) {
    char path[512];
    build_path(path, sizeof(path), ctx->output_dir, "REPORT.TXT");
    FILE *fp = fopen(path, "w");
    if (!fp) { perror(path); return -1; }

    time_t now = time(NULL);
    char timebuf[64];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", localtime(&now));

    fprintf(fp, "=============================================================\n");
    fprintf(fp, " File Parser – Processing Report\n");
    fprintf(fp, "=============================================================\n");
    fprintf(fp, " Timestamp         : %s\n", timebuf);
    fprintf(fp, " Input file        : %s\n", ctx->input_filename  ? ctx->input_filename  : "(unknown)");
    fprintf(fp, " Input format      : %s\n", ctx->format          ? ctx->format          : "(unknown)");
    fprintf(fp, " Schema file       : %s\n", ctx->schema_filename ? ctx->schema_filename : "(unknown)");
    fprintf(fp, "-------------------------------------------------------------\n");
    fprintf(fp, " Transfer status   : ");
    if (ctx->transfer_meta && ctx->transfer_meta->transfer_complete) {
        fprintf(fp, ctx->transfer_meta->transfer_ok ? "COMPLETE (OK)\n" : "COMPLETE (with errors)\n");
        if (ctx->transfer_meta->error_msg[0])
            fprintf(fp, " Transfer error    : %s\n", ctx->transfer_meta->error_msg);
    } else {
        fprintf(fp, "N/A (local/replay mode)\n");
    }
    fprintf(fp, "-------------------------------------------------------------\n");
    fprintf(fp, " Total records     : %u\n",  ctx->stats->total_records);
    fprintf(fp, " Valid records     : %u\n",  ctx->stats->valid_records);
    fprintf(fp, " Invalid records   : %u\n",  ctx->stats->invalid_records);
    if (ctx->stats->total_records > 0)
        fprintf(fp, " Validation rate   : %.1f%%\n",
                100.0 * ctx->stats->valid_records / (double)ctx->stats->total_records);
    else
        fprintf(fp, " Validation rate   : N/A (no records)\n");
    fprintf(fp, "-------------------------------------------------------------\n");
    fprintf(fp, " Error summary     : %u total errors\n", error_count());
    /* count by category */
    unsigned int cat_count[ERR_ML+1];
    memset(cat_count, 0, sizeof(cat_count));
    const ErrorEntry *errs = error_get_all();
    for (unsigned int i = 0; i < error_count(); i++) {
        int c = (int)errs[i].category;
        if (c >= 0 && c <= ERR_ML) cat_count[c]++;
    }
    for (int c = ERR_PROTOCOL; c <= ERR_ML; c++) {
        if (cat_count[c] > 0)
            fprintf(fp, "   %-18s: %u\n", category_name((ErrorCategory)c), cat_count[c]);
    }
    fprintf(fp, "-------------------------------------------------------------\n");
    if (ctx->stats->has_numeric) {
        fprintf(fp, " Numeric statistics (first feature):\n");
        fprintf(fp, "   Count  : %u\n",    ctx->stats->num_count);
        fprintf(fp, "   Min    : %.6g\n",  ctx->stats->num_min);
        fprintf(fp, "   Max    : %.6g\n",  ctx->stats->num_max);
        fprintf(fp, "   Avg    : %.6g\n",  ctx->stats->num_avg);
    } else {
        fprintf(fp, " Numeric statistics   : no numeric features found\n");
    }
    fprintf(fp, "-------------------------------------------------------------\n");
    fprintf(fp, " ML status         : %s\n", ctx->stats->ml_status);
    fprintf(fp, " ML eligible recs  : %u\n", ctx->stats->ml_eligible);
    if (ctx->stats->ml_available) {
        fprintf(fp, " Anomaly count     : %u\n", ctx->stats->anomaly_count);
        fprintf(fp, " NOTE: Anomalies are flagged for review; schema-valid\n");
        fprintf(fp, "       records are retained in PARSED.CSV regardless.\n");
    } else {
        fprintf(fp, " NOTE: ML was unavailable. ANOMALIES.CSV has headers only.\n");
        fprintf(fp, "       An empty ANOMALIES.CSV does NOT indicate zero anomalies.\n");
    }
    fprintf(fp, "=============================================================\n");
    fclose(fp);
    return 0;
}

/* ─── Public entry point ─────────────────────────────────────────────── */
int report_write_all(const ReportContext *ctx) {
    int rc = 0;
    if (ctx->write_parsed_csv) rc |= write_parsed_csv(ctx);
    if (ctx->write_parsed_txt) rc |= write_parsed_txt(ctx);
    rc |= write_errors_csv(ctx);
    rc |= write_anomalies_csv(ctx);
    rc |= write_report_txt(ctx);
    return rc;
}
