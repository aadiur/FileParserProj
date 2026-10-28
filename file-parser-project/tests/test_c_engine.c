/* ── test_c_engine.c ────────────────────────────────────────────────────────
 * Direct C unit tests for protocol, schema loader, validator, statistics,
 * error handler, and report writer.
 * ──────────────────────────────────────────────────────────────────────── */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../c_engine/record.h"
#include "../c_engine/protocol.h"
#include "../c_engine/schema_loader.h"
#include "../c_engine/validator.h"
#include "../c_engine/error_handler.h"
#include "../c_engine/statistics.h"
#include "../c_engine/report_writer.h"

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST(name) \
    do { \
        printf("Running test: %s ... ", #name); \
        g_tests_run++; \
        name(); \
        g_tests_passed++; \
        printf("PASSED\n"); \
    } while (0)

static void test_protocol_unescape(void) {
    char buf[128];
    strcpy(buf, "hello\\|world\\,foo\\=bar\\\\baz");
    proto_unescape(buf, sizeof(buf));
    assert(strcmp(buf, "hello|world,foo=bar\\baz") == 0);
}

static void test_protocol_feed_line(void) {
    ProtoContext ctx;
    proto_init(&ctx);
    Record rec;

    // Line 1: START
    int rc = proto_feed_line(&ctx, "START|TRF001|0|data.csv|csv|1\n", &rec);
    assert(rc == 0);
    assert(ctx.state == PROTO_IN_TRANSFER);
    assert(strcmp(ctx.meta.filename, "data.csv") == 0);

    // Line 2: REC
    rc = proto_feed_line(&ctx, "REC|TRF001|1|Name=Alice,Mark=88.5,Age=20\n", &rec);
    assert(rc == 1);
    assert(rec.record_id == 1);
    assert(rec.field_count == 3);
    assert(strcmp(rec.fields[0].name, "Name") == 0);
    assert(strcmp(rec.fields[0].value, "Alice") == 0);
    assert(strcmp(rec.fields[1].name, "Mark") == 0);
    assert(strcmp(rec.fields[1].value, "88.5") == 0);
    assert(strcmp(rec.fields[2].name, "Age") == 0);
    assert(strcmp(rec.fields[2].value, "20") == 0);

    // Line 3: END
    rc = proto_feed_line(&ctx, "END|TRF001|2|1\n", &rec);
    assert(rc == 0);
    assert(ctx.state == PROTO_COMPLETE);
    assert(ctx.meta.transfer_ok == 1);
}

static void test_protocol_out_of_order(void) {
    ProtoContext ctx;
    proto_init(&ctx);
    Record rec;

    proto_feed_line(&ctx, "START|TRF001|0|data.csv|csv|2\n", &rec);
    // Send seq 2 instead of 1
    int rc = proto_feed_line(&ctx, "REC|TRF001|2|Name=Bob,Mark=50,Age=22\n", &rec);
    assert(rc == -1);
    assert(ctx.state == PROTO_ERROR);
}

static void test_schema_load_and_validator(void) {
    Schema schema;
    error_handler_init();
    int rc = schema_load("../config/schema.json", &schema);
    assert(rc == 0);
    assert(schema.field_count == 3);

    validator_init(&schema);

    // Valid record
    Record r1;
    memset(&r1, 0, sizeof(r1));
    r1.record_id = 1;
    r1.field_count = 3;
    strcpy(r1.fields[0].name, "Name"); strcpy(r1.fields[0].value, "Diana");
    strcpy(r1.fields[1].name, "Mark"); strcpy(r1.fields[1].value, "15.0");
    strcpy(r1.fields[2].name, "Age");  strcpy(r1.fields[2].value, "21");
    assert(validate_record(&r1, &schema) == 1);
    assert(r1.schema_valid == 1);

    // Out of range mark (450 > 100)
    Record r2;
    memset(&r2, 0, sizeof(r2));
    r2.record_id = 2;
    r2.field_count = 3;
    strcpy(r2.fields[0].name, "Name"); strcpy(r2.fields[0].value, "Sam");
    strcpy(r2.fields[1].name, "Mark"); strcpy(r2.fields[1].value, "450.0");
    strcpy(r2.fields[2].name, "Age");  strcpy(r2.fields[2].value, "21");
    assert(validate_record(&r2, &schema) == 0);
    assert(r2.schema_valid == 0);

    // Missing required field
    Record r3;
    memset(&r3, 0, sizeof(r3));
    r3.record_id = 3;
    r3.field_count = 2;
    strcpy(r3.fields[0].name, "Name"); strcpy(r3.fields[0].value, "NoMark");
    strcpy(r3.fields[1].name, "Age");  strcpy(r3.fields[1].value, "20");
    assert(validate_record(&r3, &schema) == 0);
    assert(r3.schema_valid == 0);
}

static void test_statistics(void) {
    Schema schema;
    schema_load("../config/schema.json", &schema);

    Statistics stats;
    statistics_init(&stats);

    Record r1;
    memset(&r1, 0, sizeof(r1));
    r1.record_id = 1; r1.schema_valid = 1; r1.field_count = 2;
    strcpy(r1.fields[0].name, "Name"); strcpy(r1.fields[0].value, "A");
    strcpy(r1.fields[1].name, "Mark"); strcpy(r1.fields[1].value, "80.0");
    statistics_add_record(&stats, &r1, &schema);

    Record r2;
    memset(&r2, 0, sizeof(r2));
    r2.record_id = 2; r2.schema_valid = 1; r2.field_count = 2;
    strcpy(r2.fields[0].name, "Name"); strcpy(r2.fields[0].value, "B");
    strcpy(r2.fields[1].name, "Mark"); strcpy(r2.fields[1].value, "60.0");
    statistics_add_record(&stats, &r2, &schema);

    Record r3;
    memset(&r3, 0, sizeof(r3));
    r3.record_id = 3; r3.schema_valid = 0; // invalid record
    statistics_add_record(&stats, &r3, &schema);

    statistics_finalise(&stats);
    assert(stats.total_records == 3);
    assert(stats.valid_records == 2);
    assert(stats.invalid_records == 1);
    assert(stats.num_count == 2);
    assert(stats.num_min == 60.0);
    assert(stats.num_max == 80.0);
    assert(stats.num_avg == 70.0);
}

int main(void) {
    printf("========================================\n");
    printf(" C Engine Unit Test Suite\n");
    printf("========================================\n");

    TEST(test_protocol_unescape);
    TEST(test_protocol_feed_line);
    TEST(test_protocol_out_of_order);
    TEST(test_schema_load_and_validator);
    TEST(test_statistics);

    printf("========================================\n");
    printf(" Results: %d/%d passed\n", g_tests_passed, g_tests_run);
    printf("========================================\n");

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
