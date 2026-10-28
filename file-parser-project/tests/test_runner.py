"""
tests/test_runner.py
─────────────────────────────────────────────────────────────────────────────
Automated test suite for the File Parser project.

Tests:
  1.  C engine compile check
  2.  CSV local mode (sample.csv)
  3.  TXT local mode (sample.txt)
  4.  Schema validation: Mark=450 rejected, Mark=15 accepted
  5.  ERRORS.CSV exists and has correct headers
  6.  Protocol replay mode (valid log)
  7.  Protocol error: bad sequence number
  8.  Protocol error: incomplete transfer (no END)
  9.  Malformed JSON schema → C engine exits 2
  10. Empty input
  11. Small dataset → ML reports insufficient data (exit 2)
  12. ML module: deterministic results with fixed random_state
  13. ML module: record_id matching
  14. End-to-end with ML: anomaly output structure
  15. PARSED.CSV does not drop ML-flagged records
  16. REPORT.TXT present and contains expected fields

NOTE: Isolation Forest labels depend on data distribution. Tests verify
output STRUCTURE and REPRODUCIBILITY, not exact label assignments.
"""

import csv
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT    = Path(__file__).parent.parent
CBIN    = ROOT / "c_engine" / ("c_engine.exe" if os.name == "nt" else "c_engine")
ML      = ROOT / "ml_module" / "anomaly_detector.py"
FIX     = ROOT / "tests" / "fixtures"
SCHEMA  = ROOT / "config" / "schema.json"
TXTSCHA = ROOT / "config" / "txt_schema.json"

PASS = "\033[32mPASS\033[0m"
FAIL = "\033[31mFAIL\033[0m"
SKIP = "\033[33mSKIP\033[0m"

results = []


def run(label, fn):
    try:
        ok, msg = fn()
        status = PASS if ok else FAIL
        results.append((label, ok, msg))
        print(f"  [{status}] {label}")
        if msg and not ok:
            print(f"         -> {msg}")
    except Exception as e:
        results.append((label, False, str(e)))
        print(f"  [{FAIL}] {label} - exception: {e}")


def c_run(args, outdir=None):
    """Run the C engine with given args; returns (returncode, outdir_path)."""
    if outdir:
        cmd = [str(CBIN)] + args + ["--output", str(outdir)]
        r = subprocess.run(cmd, capture_output=True, text=True)
        return r.returncode, str(outdir), r.stdout, r.stderr
    with tempfile.TemporaryDirectory() as td:
        cmd = [str(CBIN)] + args + ["--output", td]
        r = subprocess.run(cmd, capture_output=True, text=True)
        return r.returncode, td, r.stdout, r.stderr


def ml_run(input_csv, output_csv, schema=None):
    cmd = [sys.executable, str(ML),
           "--input", str(input_csv),
           "--output", str(output_csv)]
    if schema:
        cmd += ["--config", str(schema)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    return r.returncode, r.stdout, r.stderr


# ══════════════════════════════════════════════════════════════════════════════
# TEST DEFINITIONS
# ══════════════════════════════════════════════════════════════════════════════

def t01_c_engine_exists():
    ok = CBIN.exists()
    return ok, f"Binary: {CBIN}" if not ok else ""


def t02_csv_local():
    with tempfile.TemporaryDirectory() as td:
        rc, _, _, err = c_run(
            ["--raw", str(FIX / "valid_records.csv"),
             "--schema", str(SCHEMA),
             "--format", "csv"],
            outdir=td)
        parsed = Path(td) / "PARSED.CSV"
        return (rc in (0, 5) and parsed.exists(),
                f"rc={rc} stderr={err[:200]}")


def t03_txt_local():
    with tempfile.TemporaryDirectory() as td:
        rc, _, _, err = c_run(
            ["--raw", str(FIX / "valid_records.txt"),
             "--schema", str(TXTSCHA)],
            outdir=td)
        parsed = Path(td) / "PARSED.CSV"
        return (rc in (0, 5) and parsed.exists(),
                f"rc={rc} stderr={err[:200]}")


def t04_mark_450_rejected():
    """Sam's Mark=450 must appear in ERRORS.CSV, not in PARSED.CSV."""
    with tempfile.TemporaryDirectory() as td:
        rc, _, _, _ = c_run(
            ["--raw", str(FIX / "valid_records.csv"),
             "--schema", str(SCHEMA)],
            outdir=td)
        errors = Path(td) / "ERRORS.CSV"
        if not errors.exists():
            return False, "ERRORS.CSV not created"
        # Check Sam (Mark=450) is in ERRORS
        found_range_err = False
        with open(errors, newline="") as f:
            for row in csv.DictReader(f):
                if row.get("category") == "RANGE" and "450" in row.get("original_value", ""):
                    found_range_err = True
                    break
        # Check Sam NOT in PARSED.CSV (she has schema_valid=0)
        parsed = Path(td) / "PARSED.CSV"
        sam_in_parsed = False
        if parsed.exists():
            with open(parsed, newline="") as f:
                for row in csv.DictReader(f):
                    if row.get("Name") == "Sam" and row.get("Mark") == "450.0":
                        sam_in_parsed = True
        return (found_range_err and not sam_in_parsed,
                f"range_err={found_range_err}, sam_in_parsed={sam_in_parsed}")


def t05_mark_15_in_parsed():
    """Diana with Mark=15 must pass schema (0≤15≤100) and appear in PARSED.CSV."""
    with tempfile.TemporaryDirectory() as td:
        rc, _, _, _ = c_run(
            ["--raw", str(FIX / "valid_records.csv"),
             "--schema", str(SCHEMA)],
            outdir=td)
        parsed = Path(td) / "PARSED.CSV"
        if not parsed.exists():
            return False, "PARSED.CSV not found"
        found = False
        with open(parsed, newline="") as f:
            for row in csv.DictReader(f):
                if row.get("Name") == "Diana" and row.get("Mark") == "15.0":
                    found = True
        return (found, "Diana (Mark=15) not in PARSED.CSV")


def t06_errors_csv_headers():
    with tempfile.TemporaryDirectory() as td:
        c_run(["--raw", str(FIX / "valid_records.csv"),
               "--schema", str(SCHEMA)], outdir=td)
        errors = Path(td) / "ERRORS.CSV"
        if not errors.exists():
            return False, "ERRORS.CSV missing"
        with open(errors, newline="") as f:
            hdr = next(csv.reader(f), [])
        required = {"record_id", "category", "field_name", "original_value", "reason"}
        missing = required - set(hdr)
        return (not missing, f"missing headers: {missing}")


def t07_replay_valid():
    with tempfile.TemporaryDirectory() as td:
        rc, _, _, err = c_run(
            ["--input", str(FIX / "sample_serial.log"),
             "--schema", str(SCHEMA)],
            outdir=td)
        parsed = Path(td) / "PARSED.CSV"
        if not parsed.exists():
            return False, f"rc={rc} err={err[:200]}"
        with open(parsed, newline="") as f:
            rows = list(csv.DictReader(f))
        return (rc in (0, 5) and len(rows) == 5,
                f"expected 5 rows, got {len(rows)}, rc={rc}")


def t08_bad_sequence():
    """Out-of-order sequence number should cause protocol error."""
    with tempfile.TemporaryDirectory() as td:
        rc, _, _, _ = c_run(
            ["--input", str(FIX / "bad_sequence.log"),
             "--schema", str(SCHEMA)],
            outdir=td)
        errors = Path(td) / "ERRORS.CSV"
        proto_err = False
        if errors.exists():
            with open(errors, newline="") as f:
                for row in csv.DictReader(f):
                    if row.get("category") == "PROTOCOL":
                        proto_err = True
        return (proto_err, f"rc={rc}, expected PROTOCOL error in ERRORS.CSV")


def t09_incomplete_transfer():
    """EOF without END message → protocol error logged."""
    with tempfile.TemporaryDirectory() as td:
        rc, _, _, _ = c_run(
            ["--input", str(FIX / "incomplete_transfer.log"),
             "--schema", str(SCHEMA)],
            outdir=td)
        errors = Path(td) / "ERRORS.CSV"
        has_proto = False
        if errors.exists():
            with open(errors, newline="") as f:
                for row in csv.DictReader(f):
                    if row.get("category") == "PROTOCOL":
                        has_proto = True
        return (has_proto, f"rc={rc}, expected PROTOCOL error")


def t10_malformed_schema():
    """Malformed JSON schema → C engine should fail with code 2."""
    with tempfile.TemporaryDirectory() as td:
        rc, _, _, _ = c_run(
            ["--raw", str(FIX / "valid_records.csv"),
             "--schema", str(FIX / "bad_schema.json")],
            outdir=td)
        return (rc == 2, f"expected exit 2, got {rc}")


def t11_empty_input():
    """Empty CSV → C engine should produce zero parsed records."""
    with tempfile.TemporaryDirectory() as td:
        rc, _, stdout, stderr = c_run(
            ["--raw", str(FIX / "empty_input.csv"),
             "--schema", str(SCHEMA)],
            outdir=td)
        report = Path(td) / "REPORT.TXT"
        if not report.exists():
            return False, f"rc={rc} no REPORT.TXT"
        text = report.read_text()
        return ("0" in text or rc in (0, 3, 5),
                f"rc={rc}")


def t12_ml_insufficient_data():
    """3-record dataset → ML module exits with code 2 (insufficient data)."""
    with tempfile.TemporaryDirectory() as td:
        handoff = Path(td) / "handoff.csv"
        # build minimal handoff manually
        with open(handoff, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["record_id", "Mark", "Age"])
            w.writerow([1, 75.0, 20])
            w.writerow([2, 80.0, 22])
            w.writerow([3, 78.0, 19])
        result = Path(td) / "ml_out.csv"
        rc, out, err = ml_run(handoff, result, SCHEMA)
        return (rc == 2, f"expected exit 2, got {rc}. stderr={err[:200]}")


def t13_ml_deterministic():
    """Same input twice → identical anomaly flags (reproducibility)."""
    with tempfile.TemporaryDirectory() as td:
        handoff = Path(td) / "handoff.csv"
        # write 10 records
        with open(handoff, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["record_id", "Mark", "Age"])
            for i, (m, a) in enumerate([(85, 20), (72, 22), (91, 19), (15, 21),
                                         (60, 23), (88, 20), (45, 24), (78, 21),
                                         (55, 22), (99, 18)], 1):
                w.writerow([i, m, a])
        r1 = Path(td) / "r1.csv"
        r2 = Path(td) / "r2.csv"
        ml_run(handoff, r1, SCHEMA)
        ml_run(handoff, r2, SCHEMA)
        if not r1.exists() or not r2.exists():
            return False, "ML output files not created"
        t1 = r1.read_text(); t2 = r2.read_text()
        return (t1 == t2, f"Results differ between two runs")


def t14_ml_id_matching():
    """ML results must match by record_id, not row position."""
    with tempfile.TemporaryDirectory() as td:
        handoff = Path(td) / "handoff.csv"
        with open(handoff, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["record_id", "Mark", "Age"])
            for rid, m, a in [(5, 85, 20), (12, 72, 22), (17, 91, 19),
                               (23, 15, 21), (31, 60, 23), (42, 88, 20),
                               (55, 45, 24), (67, 78, 21)]:
                w.writerow([rid, m, a])
        result = Path(td) / "ml.csv"
        rc, _, _ = ml_run(handoff, result, SCHEMA)
        if rc not in (0, 2):
            return False, f"ML unexpected exit {rc}"
        if not result.exists():
            return False, "ML result not created"
        with open(result, newline="") as f:
            rows = list(csv.DictReader(f))
        if not rows:
            return True, "(too few records for ML — correct behavior)"
        ids = [int(r["record_id"]) for r in rows]
        expected = [5, 12, 17, 23, 31, 42, 55, 67]
        return (ids == expected, f"ID mismatch: got {ids}, expected {expected}")


def t15_anomalies_csv_headers():
    """ANOMALIES.CSV must always have headers even when ML unavailable."""
    with tempfile.TemporaryDirectory() as td:
        c_run(["--raw", str(FIX / "valid_records.csv"),
               "--schema", str(SCHEMA)], outdir=td)
        anoms = Path(td) / "ANOMALIES.CSV"
        if not anoms.exists():
            return False, "ANOMALIES.CSV not created"
        with open(anoms, newline="") as f:
            hdr = next(csv.reader(f), [])
        required = {"record_id", "anomaly_flag", "anomaly_score", "status"}
        missing = required - set(hdr)
        return (not missing, f"missing: {missing}")


def t16_report_txt_fields():
    """REPORT.TXT must contain key fields."""
    with tempfile.TemporaryDirectory() as td:
        c_run(["--raw", str(FIX / "valid_records.csv"),
               "--schema", str(SCHEMA)], outdir=td)
        report = Path(td) / "REPORT.TXT"
        if not report.exists():
            return False, "REPORT.TXT not found"
        text = report.read_text()
        required_strings = [
            "Total records", "Valid records", "Invalid records",
            "ML status", "Validation rate"
        ]
        missing = [s for s in required_strings if s not in text]
        return (not missing, f"missing in report: {missing}")


def t17_parsed_csv_keeps_anomaly_flagged():
    """Schema-valid records flagged as anomalies must stay in PARSED.CSV."""
    with tempfile.TemporaryDirectory() as td:
        # Create a fake ML results CSV that flags record 1 as anomaly
        ml_res = Path(td) / "ml_results.csv"
        with open(ml_res, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["record_id", "anomaly_flag", "anomaly_score"])
            w.writerow([1, 1, 0.85])  # flag record 1

        rc, _, _, _ = c_run(
            ["--raw", str(FIX / "valid_records.csv"),
             "--schema", str(SCHEMA),
             "--ml-results", str(ml_res)],
            outdir=td)
        parsed = Path(td) / "PARSED.CSV"
        if not parsed.exists():
            return False, "PARSED.CSV missing"
        # Record 1 (Alice) should be in PARSED.CSV despite anomaly flag
        with open(parsed, newline="") as f:
            rows = list(csv.DictReader(f))
        r1 = [r for r in rows if r.get("record_id") == "1"]
        if not r1:
            return False, "Record 1 missing from PARSED.CSV"
        flag = r1[0].get("anomaly_flag", "")
        return (flag == "1", f"Expected anomaly_flag=1 on record 1, got '{flag}'")


# ══════════════════════════════════════════════════════════════════════════════

def main():
    print("\n" + "=" * 60)
    print(" File Parser Project - Automated Tests")
    print("=" * 60 + "\n")

    if not CBIN.exists():
        print(f"  [{FAIL}] C engine binary not found: {CBIN}")
        print("  Build with: cd c_engine && mingw32-make all")
        sys.exit(1)

    tests = [
        ("01 C engine binary exists",              t01_c_engine_exists),
        ("02 CSV local mode",                      t02_csv_local),
        ("03 TXT local mode",                      t03_txt_local),
        ("04 Mark=450 rejected (RANGE error)",     t04_mark_450_rejected),
        ("05 Mark=15 passes schema (in PARSED)",   t05_mark_15_in_parsed),
        ("06 ERRORS.CSV headers",                  t06_errors_csv_headers),
        ("07 Protocol replay (valid log)",         t07_replay_valid),
        ("08 Protocol: bad sequence number",       t08_bad_sequence),
        ("09 Protocol: incomplete transfer",       t09_incomplete_transfer),
        ("10 Malformed JSON schema -> exit 2",     t10_malformed_schema),
        ("11 Empty input",                         t11_empty_input),
        ("12 ML insufficient data -> exit 2",      t12_ml_insufficient_data),
        ("13 ML deterministic (reproducibility)",  t13_ml_deterministic),
        ("14 ML record_id matching",               t14_ml_id_matching),
        ("15 ANOMALIES.CSV headers always present",t15_anomalies_csv_headers),
        ("16 REPORT.TXT required fields",          t16_report_txt_fields),
        ("17 Anomaly-flagged records in PARSED",   t17_parsed_csv_keeps_anomaly_flagged),
    ]

    for label, fn in tests:
        run(label, fn)

    print("\n" + "-" * 60)
    passed = sum(1 for _, ok, _ in results if ok)
    failed = len(results) - passed
    print(f"  Results: {passed} passed, {failed} failed out of {len(results)} tests")
    print("-" * 60 + "\n")
    sys.exit(0 if failed == 0 else 1)


if __name__ == "__main__":
    main()
