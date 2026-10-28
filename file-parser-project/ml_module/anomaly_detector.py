"""
ml_module/anomaly_detector.py
──────────────────────────────────────────────────────────────────────────
Isolation Forest anomaly detector for the File Parser project.

INPUT  (--input):
    CSV file with columns: record_id, <numeric_feature1>, <numeric_feature2>, ...
    First row is a header. Produced by the C engine (valid records only).

OUTPUT (--output):
    CSV with columns: record_id, anomaly_flag, anomaly_score
    anomaly_flag : 1 = anomaly, 0 = normal
    anomaly_score: -score_samples() value → HIGHER = MORE unusual
                   Consistent convention throughout the project.

SAFEGUARDS:
    - Empty dataset: exits with code 2.
    - Too few records (< MIN_SAMPLES): exits with code 2.
    - No usable numeric features: exits with code 2.
    - Constant-valued columns are dropped silently with a warning.
    - NaN / Inf values are replaced with column median (or 0 if all NaN).
    - ML failure does NOT crash; outputs a status file instead.

Exit codes:
    0 – success
    1 – argument / I/O error
    2 – insufficient data for meaningful analysis
    3 – unexpected ML failure
"""

import argparse
import csv
import json
import math
import os
import sys

import numpy as np
import pandas as pd
from sklearn.ensemble import IsolationForest

MIN_SAMPLES = 5  # minimum records for meaningful Isolation Forest


def parse_args():
    p = argparse.ArgumentParser(description="Isolation Forest anomaly detector")
    p.add_argument("--input",  required=True,  help="Path to handoff CSV from C engine")
    p.add_argument("--output", required=True,  help="Path to write ML results CSV")
    p.add_argument("--config", required=False, help="Path to JSON schema (for IF params)")
    p.add_argument("--features", nargs="+",   help="Numeric feature column names")
    p.add_argument("--contamination", type=float, default=None)
    p.add_argument("--n-estimators",  type=int,   default=None)
    p.add_argument("--random-state",  type=int,   default=None)
    return p.parse_args()


def load_config(path):
    """Load IF parameters from the project JSON config."""
    defaults = {"if_contamination": 0.1, "if_n_estimators": 100, "if_random_state": 42}
    if not path or not os.path.isfile(path):
        return defaults
    try:
        with open(path, "r", encoding="utf-8") as f:
            cfg = json.load(f)
        defaults["if_contamination"] = cfg.get("if_contamination", defaults["if_contamination"])
        defaults["if_n_estimators"]  = cfg.get("if_n_estimators",  defaults["if_n_estimators"])
        defaults["if_random_state"]  = cfg.get("if_random_state",  defaults["if_random_state"])
    except Exception as e:
        print(f"[ML] Warning: could not load config ({e}); using defaults.", file=sys.stderr)
    return defaults


def write_error_status(output_path, message):
    """Write a minimal CSV indicating ML did not run."""
    with open(output_path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["record_id", "anomaly_flag", "anomaly_score"])
    print(f"[ML] {message}", file=sys.stderr)


def main():
    args = parse_args()

    # ── Load config
    cfg = load_config(args.config)
    contamination = args.contamination if args.contamination is not None else cfg["if_contamination"]
    n_estimators  = args.n_estimators  if args.n_estimators  is not None else cfg["if_n_estimators"]
    random_state  = args.random_state  if args.random_state  is not None else cfg["if_random_state"]

    # Clamp contamination
    if contamination <= 0 or contamination >= 0.5:
        print("[ML] Warning: contamination out of (0, 0.5); clamping to 0.1.", file=sys.stderr)
        contamination = 0.1

    # ── Load input
    if not os.path.isfile(args.input):
        print(f"[ML] ERROR: input file not found: {args.input}", file=sys.stderr)
        write_error_status(args.output, "input file not found")
        sys.exit(1)

    try:
        df = pd.read_csv(args.input)
    except Exception as e:
        print(f"[ML] ERROR: cannot read input CSV: {e}", file=sys.stderr)
        write_error_status(args.output, f"cannot read input: {e}")
        sys.exit(1)

    if df.empty or len(df) == 0:
        write_error_status(args.output, "empty dataset – ML not run")
        sys.exit(2)

    if "record_id" not in df.columns:
        print("[ML] ERROR: input CSV must have a 'record_id' column.", file=sys.stderr)
        write_error_status(args.output, "no record_id column")
        sys.exit(1)

    # ── Select feature columns
    if args.features:
        feature_cols = [c for c in args.features if c in df.columns]
    else:
        # use all numeric columns except record_id
        feature_cols = [c for c in df.columns
                        if c != "record_id" and pd.api.types.is_numeric_dtype(df[c])]

    if not feature_cols:
        write_error_status(args.output, "no usable numeric feature columns found")
        sys.exit(2)

    X = df[feature_cols].copy()

    # ── Handle NaN / Inf
    X = X.replace([np.inf, -np.inf], np.nan)
    for col in X.columns:
        med = X[col].median()
        fill = med if not math.isnan(med) else 0.0
        X[col] = X[col].fillna(fill)

    # ── Drop constant columns
    constant_cols = [c for c in X.columns if X[c].nunique() <= 1]
    if constant_cols:
        print(f"[ML] Warning: dropping constant columns: {constant_cols}", file=sys.stderr)
        X = X.drop(columns=constant_cols)
        feature_cols = [c for c in feature_cols if c not in constant_cols]

    if X.empty or len(X.columns) == 0:
        write_error_status(args.output, "all feature columns were constant – ML not run")
        sys.exit(2)

    n = len(X)
    if n < MIN_SAMPLES:
        write_error_status(
            args.output,
            f"only {n} eligible records – fewer than minimum {MIN_SAMPLES} for "
            f"meaningful Isolation Forest analysis. Results would be unreliable."
        )
        print(f"[ML] Insufficient data: {n} records < {MIN_SAMPLES} minimum.", file=sys.stderr)
        sys.exit(2)

    # ── Fit Isolation Forest
    try:
        clf = IsolationForest(
            n_estimators=n_estimators,
            contamination=contamination,
            random_state=random_state
        )
        clf.fit(X)
        labels = clf.predict(X)          # 1 = normal, -1 = anomaly
        scores = -clf.score_samples(X)  # higher = more anomalous
    except Exception as e:
        print(f"[ML] ERROR: Isolation Forest failed: {e}", file=sys.stderr)
        write_error_status(args.output, f"IsolationForest error: {e}")
        sys.exit(3)

    # ── Write results
    # anomaly_flag: 1 = anomaly (sklearn -1), 0 = normal (sklearn +1)
    anomaly_flags = [1 if lbl == -1 else 0 for lbl in labels]

    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    with open(args.output, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["record_id", "anomaly_flag", "anomaly_score"])
        for i, (rid, flag, score) in enumerate(
                zip(df["record_id"], anomaly_flags, scores)):
            w.writerow([int(rid), int(flag), f"{score:.6f}"])

    n_anomalies = sum(anomaly_flags)
    print(f"[ML] Isolation Forest complete: {n} records, {n_anomalies} anomalies flagged.")
    print(f"[ML] Features used: {feature_cols}")
    print(f"[ML] Parameters: contamination={contamination}, "
          f"n_estimators={n_estimators}, random_state={random_state}")
    print(f"[ML] Results written to: {args.output}")
    sys.exit(0)


if __name__ == "__main__":
    main()
