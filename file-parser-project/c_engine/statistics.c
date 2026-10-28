/* ── statistics.c ─────────────────────────────────────────────────────── */
#include "statistics.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <float.h>
#include <math.h>

void statistics_init(Statistics *stats) {
    memset(stats, 0, sizeof(*stats));
    stats->num_min       = DBL_MAX;
    stats->num_max       = -DBL_MAX;
    stats->num_sum       = 0.0;
    stats->num_count     = 0;
    stats->has_numeric   = 0;
    stats->ml_available  = 0;
    strcpy(stats->ml_status, "not run");
}

void statistics_add_record(Statistics *stats, const Record *rec, const Schema *schema) {
    stats->total_records++;
    if (rec->schema_valid) {
        stats->valid_records++;
        /* find first numeric feature value */
        if (schema->numeric_feature_count > 0) {
            const char *feat_name = schema->numeric_features[0];
            for (unsigned int fi = 0; fi < rec->field_count; fi++) {
                if (strcmp(rec->fields[fi].name, feat_name) == 0) {
                    char *end;
                    double v = strtod(rec->fields[fi].value, &end);
                    if (*end == '\0' && isfinite(v)) {
                        if (!stats->has_numeric) stats->has_numeric = 1;
                        if (v < stats->num_min) stats->num_min = v;
                        if (v > stats->num_max) stats->num_max = v;
                        stats->num_sum += v;
                        stats->num_count++;
                    }
                    break;
                }
            }
        }
    } else {
        stats->invalid_records++;
    }
    if (rec->is_anomaly == 1) stats->anomaly_count++;
}

void statistics_finalise(Statistics *stats) {
    if (stats->num_count > 0)
        stats->num_avg = stats->num_sum / (double)stats->num_count;
    else {
        stats->num_min = 0.0;
        stats->num_max = 0.0;
        stats->num_avg = 0.0;
    }
}

void statistics_print(const Statistics *stats) {
    printf("  Total records   : %u\n", stats->total_records);
    printf("  Valid records   : %u\n", stats->valid_records);
    printf("  Invalid records : %u\n", stats->invalid_records);
    if (stats->total_records > 0)
        printf("  Valid %%         : %.1f%%\n",
               100.0 * stats->valid_records / (double)stats->total_records);
    if (stats->has_numeric) {
        printf("  Numeric min     : %.4g\n", stats->num_min);
        printf("  Numeric max     : %.4g\n", stats->num_max);
        printf("  Numeric avg     : %.4g\n", stats->num_avg);
        printf("  Numeric count   : %u\n",   stats->num_count);
    }
    printf("  ML status       : %s\n", stats->ml_status);
    if (stats->ml_available)
        printf("  Anomaly count   : %u  (of %u eligible)\n",
               stats->anomaly_count, stats->ml_eligible);
}
