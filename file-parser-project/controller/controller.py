"""
controller/controller.py
─────────────────────────────────────────────────────────────────────────────
Orchestrates the full processing pipeline.

Responsibilities:
  - Stage input file for virtual SD card.
  - Build the ML handoff CSV from valid C-engine output.
  - Run the ML module.
  - Feed ML results back to the C engine for final output.
  - Check exit codes; never silently swallow failures.

NOT responsible for: CSV parsing, schema validation, statistics,
                     report generation (those are in the C engine).
"""

import csv
import json
import os
import subprocess
import sys
from pathlib import Path
from typing import Optional, Tuple

# ── Path resolution ───────────────────────────────────────────────────────
ROOT_DIR     = Path(__file__).parent.parent
C_ENGINE_DIR = ROOT_DIR / "c_engine"
ML_MODULE    = ROOT_DIR / "ml_module" / "anomaly_detector.py"
SDCARD_DIR   = ROOT_DIR / "arduino" / "sdcard"
OUTPUT_DIR   = ROOT_DIR / "output"

# binary name differs by OS
C_BIN = C_ENGINE_DIR / ("c_engine.exe" if os.name == "nt" else "c_engine")


def _ensure_output_dir():
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)


def _c_engine_exists() -> bool:
    return C_BIN.exists()


def _schema_fields(schema_path: str):
    """Return (numeric_feature_cols, all_fields) from schema JSON."""
    with open(schema_path, "r", encoding="utf-8") as f:
        cfg = json.load(f)
    fields = cfg.get("fields", [])
    numeric = [fld["name"] for fld in fields
               if fld.get("numeric_feature") is True]
    all_names = [fld["name"] for fld in fields]
    return numeric, all_names


def _build_ml_handoff(parsed_csv: Path, schema_path: str,
                       handoff_path: Path) -> int:
    """
    Build a CSV for the ML module from PARSED.CSV (valid records only).
    Contains: record_id + numeric feature columns.
    Returns number of eligible records written.
    """
    numeric_cols, _ = _schema_fields(schema_path)
    if not numeric_cols:
        return 0

    if not parsed_csv.exists():
        print(f"[controller] PARSED.CSV not found at {parsed_csv}", file=sys.stderr)
        return 0

    rows_out = []
    with open(parsed_csv, "r", newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            out = {"record_id": row.get("record_id", "")}
            ok = True
            for col in numeric_cols:
                val = row.get(col, "").strip()
                if val in ("", "N/A"):
                    ok = False
                    break
                try:
                    float(val)
                    out[col] = val
                except ValueError:
                    ok = False
                    break
            if ok:
                rows_out.append(out)

    if not rows_out:
        return 0

    handoff_path.parent.mkdir(parents=True, exist_ok=True)
    with open(handoff_path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=["record_id"] + numeric_cols)
        w.writeheader()
        w.writerows(rows_out)

    return len(rows_out)


def run_c_engine(input_path: Optional[str],
                  schema_path: str,
                  output_dir: str,
                  ml_results_path: Optional[str] = None,
                  raw_path: Optional[str] = None,
                  fmt: Optional[str] = None) -> int:
    """
    Run the C engine.  Returns exit code.
    Uses --raw for local CSV/TXT mode; --input for protocol replay/simulation.
    """
    if not _c_engine_exists():
        print(f"[controller] ERROR: C engine binary not found at {C_BIN}", file=sys.stderr)
        print("[controller] Build it first: cd c_engine && mingw32-make all", file=sys.stderr)
        return 99

    cmd = [str(C_BIN), "--schema", schema_path, "--output", output_dir]
    if raw_path:
        cmd += ["--raw", raw_path]
    elif input_path:
        cmd += ["--input", input_path]
    if ml_results_path and os.path.isfile(ml_results_path):
        cmd += ["--ml-results", ml_results_path]
    if fmt:
        cmd += ["--format", fmt]

    print(f"[controller] Running C engine: {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=False)
    return result.returncode


def run_ml(handoff_path: Path, ml_results_path: Path,
            schema_path: str) -> int:
    """Run the ML module. Returns exit code."""
    cmd = [
        sys.executable, str(ML_MODULE),
        "--input",  str(handoff_path),
        "--output", str(ml_results_path),
        "--config", schema_path,
    ]
    print(f"[controller] Running ML module: {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=False)
    return result.returncode


def process(input_path: str, schema_path: str,
             mode: str = "local") -> Tuple[bool, str]:
    """
    Full pipeline:
      1. Run C engine (raw mode) to produce PARSED.CSV.
      2. Build ML handoff from PARSED.CSV.
      3. Run ML module.
      4. Re-run C engine with --ml-results to incorporate anomaly flags.
      5. Return (success, output_dir).
    """
    _ensure_output_dir()
    output_dir   = str(OUTPUT_DIR)
    handoff_path = OUTPUT_DIR / "_ml_handoff.csv"
    ml_results   = OUTPUT_DIR / "_ml_results.csv"

    # Stage for Wokwi if simulation mode
    if mode == "simulation":
        from controller.staging import stage_file
        staged = stage_file(input_path, str(SDCARD_DIR))
        print(f"[controller] Staged file: {staged}")
        print("[controller] NOTE: Start the Wokwi simulation in VS Code manually.")
        print("[controller] Then re-run with --mode replay once serial output is captured.")
        return False, output_dir

    # Determine if using protocol replay or raw file
    is_replay = (mode == "replay")

    # Step 1: Initial C engine run (no ML results yet)
    fmt = None
    if schema_path:
        try:
            with open(schema_path) as f:
                cfg = json.load(f)
            fmt = cfg.get("format")
        except Exception:
            pass

    rc1 = run_c_engine(
        input_path  = input_path if is_replay else None,
        schema_path = schema_path,
        output_dir  = output_dir,
        raw_path    = input_path if not is_replay else None,
        fmt         = fmt,
    )
    if rc1 not in (0, 5):
        print(f"[controller] C engine step 1 failed with code {rc1}.", file=sys.stderr)
        return False, output_dir

    # Step 2: Build ML handoff
    parsed_csv = OUTPUT_DIR / "PARSED.CSV"
    n_eligible = _build_ml_handoff(parsed_csv, schema_path, handoff_path)
    print(f"[controller] ML handoff: {n_eligible} eligible records.")

    # Step 3: Run ML if there are eligible records
    ml_ok = False
    if n_eligible > 0:
        rc_ml = run_ml(handoff_path, ml_results, schema_path)
        ml_ok = (rc_ml == 0)
        if not ml_ok:
            print(f"[controller] ML module exited with code {rc_ml} "
                  f"(may indicate insufficient data or non-fatal condition).")
    else:
        print("[controller] No ML-eligible records; skipping ML module.")

    # Step 4: Re-run C engine with ML results if available
    if ml_ok and ml_results.exists():
        rc2 = run_c_engine(
            input_path     = input_path if is_replay else None,
            schema_path    = schema_path,
            output_dir     = output_dir,
            ml_results_path= str(ml_results),
            raw_path       = input_path if not is_replay else None,
            fmt            = fmt,
        )
        if rc2 not in (0, 5):
            print(f"[controller] C engine step 2 (with ML) failed with code {rc2}.",
                  file=sys.stderr)
    else:
        print("[controller] Skipping C engine re-run (no ML results).")

    return True, output_dir
