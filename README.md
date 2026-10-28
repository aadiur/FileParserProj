# User-Configurable Multi-Format File Parser with ML-Based Anomaly Detection

A production-grade, modular term project integrating:
- **Embedded Simulation**: Arduino Uno firmware (`sketch.ino`) reading CSV/TXT files from a virtual microSD card via SPI and transmitting over serial in Wokwi.
- **Serial Wire Protocol**: Documented, framed, sequence-checked protocol with start, record, end, and error message boundaries.
- **Native C Processing Engine**: Portable C11 engine performing protocol parsing, user-configurable JSON schema validation, error tracking, descriptive statistics, and report generation.
- **Python ML Module**: Scikit-Learn `IsolationForest` anomaly detector processing schema-valid numeric records and scoring outliers with safeguards for small datasets.
- **Python Controller / Orchestrator**: Multi-mode pipeline coordinator supporting Local CSV, Local TXT, Protocol Replay, and Wokwi Simulation modes.

---

## 1. Architecture & Data Flow

```
[ Input File (CSV / TXT) ] + [ JSON Schema Config ]
                  │
                  ▼
         [ Python Controller ]
                  │ (Stages file for virtual microSD)
                  ▼
   [ Simulated Arduino Uno (Wokwi) ]
         │ (Reads SPI microSD, formats records)
         ▼
   [ Serial Protocol (Framed text lines) ]
   START | TRANSFER_ID | SEQ | FILENAME | FORMAT | COUNT
   REC   | TRANSFER_ID | SEQ | field1=val1,field2=val2...
   END   | TRANSFER_ID | SEQ | TOTAL_SENT
         │
         ▼
     [ Native C Processing Engine ]
         ├─ protocol.c      -> Sequence & frame decoding
         ├─ schema_loader.c -> Parses JSON schema via jsmn
         ├─ validator.c     -> Required fields, types, min/max, duplicates
         ├─ error_handler.c -> ERRORS.CSV logging
         └─ statistics.c    -> Min, max, avg, counts
                  │
                  ▼ (Writes schema-valid numeric records to handoff CSV)
     [ Python ML Module (Isolation Forest) ]
         │ (Calculates anomaly flags & scores: -score_samples)
         ▼
     [ Native C Processing Engine ]
         │ (Matches records by record_id; integrates ML scores)
         ▼
    [ Output Files ]
    - PARSED.CSV / PARSED.TXT
    - ERRORS.CSV
    - ANOMALIES.CSV
    - REPORT.TXT
```

---

## 2. Project Structure

```
file-parser-project/
├── .gitignore
├── requirements.txt
├── README.md
├── run.py                          <- Main CLI entry point
├── arduino/
│   ├── sketch.ino                  <- Arduino Uno microSD reader & serial transmitter
│   ├── diagram.json                <- Wokwi parts and wiring (Uno + microSD)
│   ├── wokwi.toml                  <- Wokwi simulator configuration
│   └── sdcard/                     <- Virtual microSD staging directory
│       ├── sample.csv
│       └── sample.txt
├── controller/
│   ├── controller.py               <- Pipeline execution orchestrator
│   ├── serial_bridge.py            <- RFC2217 serial reader
│   └── staging.py                  <- File staging into arduino/sdcard/
├── c_engine/
│   ├── Makefile                    <- GCC/MinGW build configuration
│   ├── record.h                    <- Core data structures & bounds
│   ├── jsmn.h                      <- Embedded JSON tokenizer header
│   ├── protocol.h / protocol.c     <- Wire protocol parser & generator
│   ├── schema_loader.h / .c        <- JSON schema parser using jsmn
│   ├── validator.h / validator.c   <- Schema, type, range, duplicate validator
│   ├── error_handler.h / .c        <- Bounded error tracking system
│   ├── statistics.h / statistics.c <- Descriptive statistics calculator
│   ├── report_writer.h / .c        <- CSV/TXT report generator
│   └── main.c                      <- C engine entry point
├── ml_module/
│   ├── anomaly_detector.py         <- Isolation Forest model implementation
│   └── requirements.txt            <- scikit-learn, numpy, pandas
├── config/
│   ├── schema.json                 <- Config for CSV format (with min/max constraints)
│   └── txt_schema.json             <- Config for structured pipe-delimited TXT format
├── input/
│   ├── sample.csv                  <- Sample student records (includes Sam with Mark=450)
│   └── sample.txt                  <- Sample TXT records
├── output/                         <- Generated report files
└── tests/
    ├── test_runner.py              <- 17 automated end-to-end Python tests
    ├── test_c_engine.c             <- Direct C unit test suite
    └── fixtures/                   <- Test datasets & serial capture logs
```

---

## 3. Serial Protocol Specification

Every message is transmitted as an ASCII line terminated with `\n` (LF):

```
MSG_TYPE|TRANSFER_ID|SEQ|PAYLOAD
```

- **Delimiters**: Pipe `|` separates header fields. Values containing `|`, `\`, `,`, or `=` are escaped with a preceding backslash `\`.
- **Message Types**:
  - `START|TRF001|0|<filename>|<format>|<expected_count>`: Initiates transfer session. Sequence number is always 0.
  - `REC|TRF001|<seq>|<name1>=<val1>,<name2>=<val2>,...`: Record data payload. Sequence numbers are 1-based and strictly monotonic.
  - `END|TRF001|<seq>|<total_records_sent>`: Marks completion. Count must match received records.
  - `ERR|TRF001|0|<error_description>`: Firmware error notification.

---

## 4. Building and Installation

### Prerequisites
- **C Compiler**: GCC (MinGW on Windows, or standard GCC/Clang on Linux/macOS)
- **Make**: `mingw32-make` (Windows) or `make` (Linux/macOS)
- **Python**: Python 3.10+ (tested with Python 3.13)

### Installation
Install Python dependencies:
```bash
pip install -r requirements.txt
# or via Windows py launcher:
py -m pip install -r requirements.txt
```

### Build C Engine
```bash
cd c_engine
mingw32-make all    # On Windows
make all            # On Linux/macOS
```

---

## 5. Execution Modes

From `file-parser-project/`:

### Mode 1: Local CSV Mode
Processes a local CSV file directly through the C engine and ML module:
```bash
py run.py --mode local --input input/sample.csv --schema config/schema.json
```

### Mode 2: Local Structured TXT Mode
Processes a pipe-delimited text file:
```bash
py run.py --mode local --input input/sample.txt --schema config/txt_schema.json
```

### Mode 3: Replay Mode
Replays captured Arduino serial log files through the protocol parser:
```bash
py run.py --mode replay --input tests/fixtures/sample_serial.log --schema config/schema.json
```

### Mode 4: Custom Input
Process any custom dataset and schema:
```bash
py run.py --mode custom --input <path_to_data> --schema <path_to_schema>
```

### Mode 5: Wokwi Simulation Mode
Stages the input file into `arduino/sdcard/` for the simulated microSD card:
```bash
py run.py --mode simulation --input input/sample.csv --schema config/schema.json
```
Follow the on-screen instructions to run the simulation in Wokwi for VS Code.

---

## 6. Running Tests

### Automated Integration & Pipeline Test Suite (17 Tests)
```bash
py run.py --test
# or directly:
py tests/test_runner.py
```

### C Engine Unit Tests
```bash
cd c_engine
mingw32-make test   # Windows
make test           # Linux/macOS
```

---

## 7. Output Files

Generated in the `output/` directory:
- **`PARSED.CSV`**: All schema-valid records normalized with assigned `record_id`, original fields, and ML anomaly flag and score columns. (Anomalies are retained for review).
- **`PARSED.TXT`**: Formatted text output when configured.
- **`ERRORS.CSV`**: Validation and protocol errors detailing record ID, error category (`RANGE`, `TYPE`, `MISSING_FIELD`, `DUPLICATE`, `PROTOCOL`), field name, rejected value, and reason.
- **`ANOMALIES.CSV`**: Schema-valid records flagged as anomalous by the Isolation Forest model, with anomaly score and reviewed status.
- **`REPORT.TXT`**: Complete processing report with timestamp, record counts, validation rates, error categories, numeric statistics (min/max/avg), and ML execution status.
