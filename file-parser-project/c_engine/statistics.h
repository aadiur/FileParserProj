#ifndef STATISTICS_H
#define STATISTICS_H

#include "record.h"
#include "schema_loader.h"

/* ── statistics.h ─────────────────────────────────────────────────────── */

void statistics_init(Statistics *stats);

/* Process one record into the running stats.
 * Uses the first configured numeric_feature field for numeric stats. */
void statistics_add_record(Statistics *stats, const Record *rec, const Schema *schema);

/* Finalise averages etc. — call once after all records processed */
void statistics_finalise(Statistics *stats);

/* Print stats to stdout */
void statistics_print(const Statistics *stats);

#endif /* STATISTICS_H */
