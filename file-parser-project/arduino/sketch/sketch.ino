/*
 * sketch.ino – Arduino Uno / Wokwi simulation
 * Reads a CSV or TXT file from a virtual microSD card via SPI and
 * transmits records over serial using the documented pipe-delimited protocol.
 *
 * WIRE PROTOCOL:
 *   MSG_TYPE|TRANSFER_ID|SEQ|PAYLOAD\n
 *   START|TRF001|0|filename|format|expected_count
 *   REC|TRF001|<seq>|field1_name=field1_val,field2_name=field2_val,...
 *   END|TRF001|<last_seq+1>|<total_sent>
 *   ERR|TRF001|0|error_description
 */

#include <SPI.h>
#include <SD.h>

#define SD_CS_PIN     10
#define BAUD_RATE     115200
#define TRANSFER_ID   "TRF001"

const char FILE_FORMAT[]  = "csv";
const char FILENAME[]     = "sample.csv";
const bool HAS_HEADER     = true;
const char TXT_DELIMITER  = '|';

const char* TXT_FIELDS[] = { "Name", "Mark", "Age" };
const int   TXT_FIELD_COUNT = 3;

#define MAX_LINE_LEN  256
#define MAX_FIELDS     20
#define MAX_FIELD_LEN  64

/* Built-in fallback sample data in flash memory */
const char f0[] PROGMEM = "Name=Alice,Mark=85.5,Age=20";
const char f1[] PROGMEM = "Name=Bob,Mark=72.0,Age=22";
const char f2[] PROGMEM = "Name=Charlie,Mark=91.0,Age=19";
const char f3[] PROGMEM = "Name=Diana,Mark=15.0,Age=21";
const char f4[] PROGMEM = "Name=Eve,Mark=60.5,Age=23";
const char f5[] PROGMEM = "Name=Frank,Mark=88.0,Age=20";
const char f6[] PROGMEM = "Name=Grace,Mark=45.0,Age=24";
const char f7[] PROGMEM = "Name=Hank,Mark=78.5,Age=21";
const char f8[] PROGMEM = "Name=Ivy,Mark=55.0,Age=22";
const char f9[] PROGMEM = "Name=Jack,Mark=95.0,Age=18";
const char f10[] PROGMEM = "Name=Kate,Mark=30.0,Age=25";
const char f11[] PROGMEM = "Name=Liam,Mark=67.0,Age=23";
const char f12[] PROGMEM = "Name=Mia,Mark=82.0,Age=20";
const char f13[] PROGMEM = "Name=Noah,Mark=12.0,Age=19";
const char f14[] PROGMEM = "Name=Olivia,Mark=73.5,Age=22";
const char f15[] PROGMEM = "Name=Paul,Mark=58.0,Age=21";
const char f16[] PROGMEM = "Name=Quinn,Mark=99.0,Age=18";
const char f17[] PROGMEM = "Name=Rose,Mark=44.0,Age=26";
const char f18[] PROGMEM = "Name=Sam,Mark=450.0,Age=21";
const char f19[] PROGMEM = "Name=Tina,Mark=71.0,Age=22";

const char* const FALLBACK_RECORDS[] PROGMEM = {
  f0, f1, f2, f3, f4, f5, f6, f7, f8, f9,
  f10, f11, f12, f13, f14, f15, f16, f17, f18, f19
};
const int FALLBACK_COUNT = 20;

void escapeValue(const char* src, char* dst, int dstLen) {
  int j = 0;
  for (int i = 0; src[i] != '\0' && j < dstLen - 2; i++) {
    char c = src[i];
    if (c == '\\' || c == '|' || c == ',' || c == '=') {
      dst[j++] = '\\';
    }
    dst[j++] = c;
  }
  dst[j] = '\0';
}

int splitCSV(char* line, char fields[][MAX_FIELD_LEN], int maxFields) {
  int fc = 0;
  char* p = line;
  while (*p && fc < maxFields) {
    int vp = 0;
    if (*p == '"') {
      p++;
      while (*p && vp < MAX_FIELD_LEN - 1) {
        if (*p == '"' && *(p+1) == '"') { fields[fc][vp++] = '"'; p += 2; }
        else if (*p == '"') { p++; break; }
        else fields[fc][vp++] = *p++;
      }
      if (*p == ',') p++;
    } else {
      while (*p && *p != ',' && vp < MAX_FIELD_LEN - 1)
        fields[fc][vp++] = *p++;
      if (*p == ',') p++;
    }
    fields[fc][vp] = '\0';
    fc++;
  }
  return fc;
}

int splitDelim(char* line, char delim, char fields[][MAX_FIELD_LEN], int maxFields) {
  int fc = 0;
  char* p = line;
  while (*p && fc < maxFields) {
    int vp = 0;
    while (*p && *p != delim && vp < MAX_FIELD_LEN - 1)
      fields[fc][vp++] = *p++;
    fields[fc][vp] = '\0';
    fc++;
    if (*p == delim) p++;
  }
  return fc;
}

void sendError(const char* msg) {
  Serial.print("ERR|");
  Serial.print(TRANSFER_ID);
  Serial.print("|0|");
  Serial.println(msg);
  Serial.flush();
}

void streamFallback(const char* reason) {
  Serial.print("WARN|");
  Serial.print(TRANSFER_ID);
  Serial.print("|0|");
  Serial.println(reason);

  Serial.print("START|");
  Serial.print(TRANSFER_ID);
  Serial.print("|0|");
  Serial.print(FILENAME);
  Serial.print("|");
  Serial.print(FILE_FORMAT);
  Serial.print("|");
  Serial.println(FALLBACK_COUNT);
  Serial.flush();

  char fbuf[MAX_LINE_LEN];
  for (int i = 0; i < FALLBACK_COUNT; i++) {
    delay(20);
    strcpy_P(fbuf, (char*)pgm_read_word(&(FALLBACK_RECORDS[i])));
    Serial.print("REC|");
    Serial.print(TRANSFER_ID);
    Serial.print("|");
    Serial.print(i + 1);
    Serial.print("|");
    Serial.println(fbuf);
    Serial.flush();
  }

  Serial.print("END|");
  Serial.print(TRANSFER_ID);
  Serial.print("|");
  Serial.print(FALLBACK_COUNT + 1);
  Serial.print("|");
  Serial.println(FALLBACK_COUNT);
  Serial.flush();
}

void executeTransfer() {
  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);

  if (!SD.begin(SD_CS_PIN)) {
    streamFallback("SD card not found; streaming embedded sample records");
    return;
  }

  if (!SD.exists(FILENAME)) {
    streamFallback("sample.csv not found on SD; streaming embedded sample records");
    return;
  }

  File f = SD.open(FILENAME, FILE_READ);
  if (!f || f.size() == 0) {
    if (f) f.close();
    streamFallback("sample.csv empty or unreadable; streaming embedded sample records");
    return;
  }

  /* Count records */
  int total = 0;
  bool firstLine = true;
  char line[MAX_LINE_LEN];

  while (f.available()) {
    int len = 0;
    while (f.available() && len < MAX_LINE_LEN - 1) {
      char c = f.read();
      if (c == '\n') break;
      if (c != '\r') line[len++] = c;
    }
    line[len] = '\0';
    if (len == 0 || line[0] == '#') continue;
    if (strcmp(FILE_FORMAT, "csv") == 0 && firstLine && HAS_HEADER) {
      firstLine = false;
      continue;
    }
    firstLine = false;
    total++;
  }

  f.seek(0);

  /* Send START */
  Serial.print("START|");
  Serial.print(TRANSFER_ID);
  Serial.print("|0|");
  Serial.print(FILENAME);
  Serial.print("|");
  Serial.print(FILE_FORMAT);
  Serial.print("|");
  Serial.println(total);
  Serial.flush();

  char csvFieldNames[MAX_FIELDS][MAX_FIELD_LEN];
  int  csvFieldCount = 0;
  firstLine = true;

  if (strcmp(FILE_FORMAT, "csv") == 0 && HAS_HEADER) {
    int len = 0;
    while (f.available() && len < MAX_LINE_LEN - 1) {
      char c = f.read();
      if (c == '\n') break;
      if (c != '\r') line[len++] = c;
    }
    line[len] = '\0';
    char hdrFields[MAX_FIELDS][MAX_FIELD_LEN];
    csvFieldCount = splitCSV(line, hdrFields, MAX_FIELDS);
    for (int i = 0; i < csvFieldCount && i < MAX_FIELDS; i++)
      strncpy(csvFieldNames[i], hdrFields[i], MAX_FIELD_LEN - 1);
    firstLine = false;
  }

  int seq      = 1;
  int sentRecs = 0;

  while (f.available()) {
    int len = 0;
    while (f.available() && len < MAX_LINE_LEN - 1) {
      char c = f.read();
      if (c == '\n') break;
      if (c != '\r') line[len++] = c;
    }
    line[len] = '\0';
    if (len == 0 || line[0] == '#') continue;

    char vals[MAX_FIELDS][MAX_FIELD_LEN];
    int  fc = (strcmp(FILE_FORMAT, "csv") == 0)
              ? splitCSV(line, vals, MAX_FIELDS)
              : splitDelim(line, TXT_DELIMITER, vals, MAX_FIELDS);

    char payload[MAX_LINE_LEN * 2];
    int  pp = 0;
    char eName[MAX_FIELD_LEN * 2], eVal[MAX_FIELD_LEN * 2];

    for (int i = 0; i < fc && pp < (int)sizeof(payload) - 8; i++) {
      const char* fname;
      if (strcmp(FILE_FORMAT, "csv") == 0 && csvFieldCount > 0 && i < csvFieldCount)
        fname = csvFieldNames[i];
      else if (i < TXT_FIELD_COUNT)
        fname = TXT_FIELDS[i];
      else
        fname = "extra";

      escapeValue(fname,    eName, sizeof(eName));
      escapeValue(vals[i],  eVal,  sizeof(eVal));

      if (i > 0) payload[pp++] = ',';
      pp += snprintf(payload + pp, sizeof(payload) - (size_t)pp,
                     "%s=%s", eName, eVal);
    }
    payload[pp] = '\0';

    delay(20);
    Serial.print("REC|");
    Serial.print(TRANSFER_ID);
    Serial.print("|");
    Serial.print(seq);
    Serial.print("|");
    Serial.println(payload);
    Serial.flush();

    seq++;
    sentRecs++;
  }

  f.close();

  Serial.print("END|");
  Serial.print(TRANSFER_ID);
  Serial.print("|");
  Serial.print(seq);
  Serial.print("|");
  Serial.println(sentRecs);
  Serial.flush();
}

void setup() {
  Serial.begin(BAUD_RATE);
  delay(1200);

  Serial.println(F("\n=============================================="));
  Serial.println(F(" Arduino Uno Virtual Serial Stream Online"));
  Serial.println(F("=============================================="));

  executeTransfer();
}

unsigned long lastRepeat = 0;

void loop() {
  /* Re-send every 10 seconds or when user types any key in Serial Terminal */
  if (Serial.available()) {
    while (Serial.available()) Serial.read();
    Serial.println(F("\n[Serial Command Received: Re-sending records...]"));
    executeTransfer();
    lastRepeat = millis();
  }

  if (millis() - lastRepeat > 10000) {
    lastRepeat = millis();
    Serial.println(F("[Auto-repeat] Sending records (send any key to re-trigger)..."));
    executeTransfer();
  }
}
