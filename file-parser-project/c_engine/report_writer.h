#ifndef REPORT_WRITER_H
#define REPORT_WRITER_H

#include <stddef.h>
#include "record.h"
#include "schema_loader.h"
#include "statistics.h"

/* ── report_writer.h ─────────────────────────────────────────────────── */

typedef struct {
    const char     *output_dir;
    const char     *input_filename;
    const char     *schema_filename;
    const char     *format;          /* "csv" or "txt" */
    const Record   *records;
    int             record_count;
    const Schema   *schema;
    const Statistics *stats;
    const TransferMeta *transfer_meta;
    int             write_parsed_csv;
    int             write_parsed_txt;
} ReportContext;

/* Write all output files. Returns 0 on success, -1 on any failure. */
int report_write_all(const ReportContext *ctx);

/* Escape a CSV field value (surrounds with quotes when needed) */
void csv_escape(const char *in, char *out, size_t outsz);

#endif /* REPORT_WRITER_H */
