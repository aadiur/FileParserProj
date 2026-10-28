"""
run.py  –  File Parser Project entry point
═══════════════════════════════════════════════════════════════════════════════

Usage examples:
  py run.py --mode local  --input input/sample.csv --schema config/schema.json
  py run.py --mode local  --input input/sample.txt --schema config/txt_schema.json
  py run.py --mode replay --input tests/fixtures/sample_serial.log --schema config/schema.json
  py run.py --mode simulation --input input/sample.csv --schema config/schema.json
  py run.py --mode custom --input <path> --schema <schema_path>
  py run.py --test

Modes:
  local      – process a CSV or TXT file directly (no Wokwi required)
  replay     – process a captured serial protocol log through the C engine
  simulation – stage file for Wokwi, then connect via RFC2217 (requires VS Code)
  custom     – same as local but with user-supplied paths

Exit codes:
  0 – success
  1 – argument / setup error
  2 – C engine build missing
  5 – partial success (some records invalid)
  other – processing failure
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT  = Path(__file__).parent
CBIN  = ROOT / "c_engine" / ("c_engine.exe" if os.name == "nt" else "c_engine")
ML    = ROOT / "ml_module" / "anomaly_detector.py"
OUT   = ROOT / "output"


# ── helpers ──────────────────────────────────────────────────────────────────

def check_c_engine():
    if not CBIN.exists():
        print("[run.py] ERROR: C engine binary not found.", file=sys.stderr)
        print(f"         Build it: cd c_engine && mingw32-make all", file=sys.stderr)
        sys.exit(2)


def run_c(raw=None, proto=None, schema=None, ml_results=None, fmt=None):
    """Call the C engine. Returns exit code."""
    cmd = [str(CBIN), "--schema", str(schema), "--output", str(OUT)]
    if raw:
        cmd += ["--raw", str(raw)]
    elif proto:
        cmd += ["--input", str(proto)]
    if ml_results and Path(ml_results).exists():
        cmd += ["--ml-results", str(ml_results)]
    if fmt:
        cmd += ["--format", fmt]
    print(f"\n[run.py] C engine: {' '.join(cmd)}")
    return subprocess.run(cmd).returncode


def run_ml(handoff, results, schema):
    """Call the ML module. Returns exit code."""
    cmd = [sys.executable, str(ML),
           "--input", str(handoff),
           "--output", str(results),
           "--config", str(schema)]
    print(f"\n[run.py] ML module: {' '.join(cmd)}")
    return subprocess.run(cmd).returncode


def build_ml_handoff(parsed_csv, schema_path, handoff_path):
    """Extract numeric-feature rows from PARSED.CSV for the ML module."""
    import csv, json
    try:
        with open(schema_path) as f:
            cfg = json.load(f)
        numeric_cols = [fld["name"] for fld in cfg.get("fields", [])
                        if fld.get("numeric_feature")]
    except Exception:
        numeric_cols = []

    if not numeric_cols or not Path(parsed_csv).exists():
        return 0

    rows = []
    with open(parsed_csv, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            out = {"record_id": row.get("record_id", "")}
            ok = True
            for col in numeric_cols:
                val = row.get(col, "").strip()
                if val in ("", "N/A"):
                    ok = False; break
                try:
                    float(val); out[col] = val
                except ValueError:
                    ok = False; break
            if ok:
                rows.append(out)

    if not rows:
        return 0

    Path(handoff_path).parent.mkdir(parents=True, exist_ok=True)
    with open(handoff_path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=["record_id"] + numeric_cols)
        w.writeheader(); w.writerows(rows)
    return len(rows)


def pipeline(raw=None, proto=None, schema=None, fmt=None):
    """Full pipeline: C → ML handoff → ML → C with ML results."""
    OUT.mkdir(parents=True, exist_ok=True)
    handoff  = OUT / "_ml_handoff.csv"
    ml_res   = OUT / "_ml_results.csv"

    # Step 1: C engine (no ML yet)
    rc = run_c(raw=raw, proto=proto, schema=schema, fmt=fmt)
    if rc not in (0, 5):
        print(f"\n[run.py] C engine failed (exit {rc}).", file=sys.stderr)
        return rc

    # Step 2: Build ML handoff
    n = build_ml_handoff(OUT / "PARSED.CSV", schema, handoff)
    print(f"\n[run.py] ML-eligible records: {n}")

    # Step 3: ML
    ml_ok = False
    if n > 0:
        rc_ml = run_ml(handoff, ml_res, schema)
        ml_ok = (rc_ml == 0)
        if not ml_ok:
            print(f"[run.py] ML module returned code {rc_ml} "
                  f"(insufficient data or non-fatal).")
    else:
        print("[run.py] Skipping ML (no eligible records).")

    # Step 4: C engine with ML results
    if ml_ok:
        rc = run_c(raw=raw, proto=proto, schema=schema,
                   ml_results=str(ml_res), fmt=fmt)

    print(f"\n[run.py] Output directory: {OUT.resolve()}")
    for f in ["REPORT.TXT", "PARSED.CSV", "ERRORS.CSV", "ANOMALIES.CSV"]:
        p = OUT / f
        size = p.stat().st_size if p.exists() else 0
        status_str = "[OK]" if p.exists() else "[MISSING]"
        print(f"  {f:15s}: {status_str}  ({size} bytes)")
    return rc


# ── test runner ───────────────────────────────────────────────────────────────

def run_tests():
    print("=" * 60)
    print(" File Parser Project - Test Suite")
    print("=" * 60)
    test_script = ROOT / "tests" / "test_runner.py"
    if not test_script.exists():
        print("[run.py] Test runner not found at tests/test_runner.py", file=sys.stderr)
        return 1
    rc = subprocess.run([sys.executable, str(test_script)]).returncode
    return rc


# ── main ──────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="File Parser Project – run.py",
        formatter_class=argparse.RawTextHelpFormatter
    )
    parser.add_argument("--mode", choices=["local", "replay", "simulation", "custom"],
                        default="local",
                        help="Execution mode (default: local)")
    parser.add_argument("--input",  help="Input file path (CSV, TXT, or serial log)")
    parser.add_argument("--schema", help="JSON schema config path")
    parser.add_argument("--format", dest="fmt",
                        help="Override format: csv or txt")
    parser.add_argument("--test",   action="store_true",
                        help="Run the automated test suite")
    args = parser.parse_args()

    if args.test:
        sys.exit(run_tests())

    # defaults
    if not args.input:
        args.input  = str(ROOT / "input" / "sample.csv")
        print(f"[run.py] No --input specified; using default: {args.input}")
    if not args.schema:
        args.schema = str(ROOT / "config" / "schema.json")
        print(f"[run.py] No --schema specified; using default: {args.schema}")

    if not Path(args.input).exists():
        print(f"[run.py] Input file not found: {args.input}", file=sys.stderr)
        sys.exit(1)
    if not Path(args.schema).exists():
        print(f"[run.py] Schema file not found: {args.schema}", file=sys.stderr)
        sys.exit(1)

    check_c_engine()

    if args.mode in ("local", "custom"):
        rc = pipeline(raw=args.input, schema=args.schema, fmt=args.fmt)
        sys.exit(rc if rc not in (0, 5) else 0)

    elif args.mode == "replay":
        rc = pipeline(proto=args.input, schema=args.schema, fmt=args.fmt)
        sys.exit(rc if rc not in (0, 5) else 0)

    elif args.mode == "simulation":
        # Stage the file
        from controller.staging import stage_file
        sdcard = ROOT / "arduino" / "sdcard"
        staged = stage_file(args.input, str(sdcard))
        print(f"\n[run.py] File staged: {staged}")
        print("\n[run.py] SIMULATION MODE - manual steps required:")
        print("  1. Open 'arduino/sketch.ino' in VS Code with the Wokwi extension.")
        print("  2. Press F1 -> 'Wokwi: Start Simulation'.")
        print("  3. Wait for the transfer to complete in the Wokwi serial monitor.")
        print("  4. Copy/save the serial output to tests/fixtures/sample_serial.log")
        print("  5. Re-run: py run.py --mode replay --input tests/fixtures/sample_serial.log")
        print("\n  Alternatively, if Wokwi RFC2217 is active:")

        # Try RFC2217 capture
        cap_log = str(OUT / "_serial_capture.log")
        try:
            from controller.serial_bridge import collect_serial_output
            cap = collect_serial_output(output_path=cap_log)
            print(f"\n[run.py] Serial capture saved: {cap}")
            rc = pipeline(proto=cap, schema=args.schema)
            sys.exit(rc if rc not in (0, 5) else 0)
        except Exception as e:
            print(f"\n[run.py] RFC2217 capture failed: {e}")
            print("[run.py] Wokwi simulation must be started manually first.")
            sys.exit(1)


if __name__ == "__main__":
    main()
