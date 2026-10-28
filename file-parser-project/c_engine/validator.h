#ifndef VALIDATOR_H
#define VALIDATOR_H

#include "record.h"
#include "schema_loader.h"

/* ── validator.h ─────────────────────────────────────────────────────────── */

/* Initialise validator state (clears duplicate tracking table) */
void validator_init(const Schema *schema);

/* Validate one record against the schema.
 * Sets rec->schema_valid (1 = pass, 0 = fail) and logs errors via error_handler.
 * Returns 1 if valid, 0 if invalid. */
int validate_record(Record *rec, const Schema *schema);

/* Reset duplicate-detection table */
void validator_reset(void);

#endif /* VALIDATOR_H */
