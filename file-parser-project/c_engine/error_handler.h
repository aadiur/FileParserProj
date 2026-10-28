#ifndef ERROR_HANDLER_H
#define ERROR_HANDLER_H

#include "record.h"

/* ── error_handler.h ────────────────────────────────────────────────────── */

/* Initialise the global error store */
void   error_handler_init(void);

/* Append one error entry; all strings are safely bounded */
void   error_add(unsigned int record_id,
                 ErrorCategory category,
                 const char *field_name,
                 const char *original_value,
                 const char *reason);

/* Return total errors recorded */
unsigned int error_count(void);

/* Access error array (read-only pointer) */
const ErrorEntry *error_get_all(void);

/* Clear all errors (use between runs) */
void   error_clear(void);

#endif /* ERROR_HANDLER_H */
