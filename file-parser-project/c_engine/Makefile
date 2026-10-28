# ── Makefile ─────────────────────────────────────────────────────────────
# Builds the C file parser engine.
# Tested with MinGW GCC 6.3.0 on Windows; compatible with GCC/Linux/macOS.
#
# Targets:
#   all     – build c_engine (or c_engine.exe on Windows)
#   test    – build and run the C unit tests
#   clean   – remove build artefacts
# ─────────────────────────────────────────────────────────────────────────

CC      = gcc
STD     = -std=c11
WARN    = -Wall -Wextra -Wpedantic -Wno-unused-parameter
OPT     = -O2
CFLAGS  = $(STD) $(WARN) $(OPT) -D_POSIX_C_SOURCE=200112L

# Use -lm only where needed; MinGW already links msvcrt for math
LDFLAGS = -lm

SRCS    = main.c protocol.c schema_loader.c validator.c \
          error_handler.c statistics.c report_writer.c

OBJS    = $(SRCS:.c=.o)

# Detect OS (Windows vs Unix)
ifeq ($(OS),Windows_NT)
    TARGET  = c_engine.exe
    RM      = del /Q /F
    MKDIR   = mkdir
else
    TARGET  = c_engine
    RM      = rm -f
    MKDIR   = mkdir -p
endif

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# ── Dependencies ─────────────────────────────────────────────────────────
main.o:          main.c record.h protocol.h schema_loader.h validator.h \
                 statistics.h report_writer.h error_handler.h
protocol.o:      protocol.c protocol.h error_handler.h record.h
schema_loader.o: schema_loader.c schema_loader.h error_handler.h jsmn.h record.h
validator.o:     validator.c validator.h error_handler.h record.h schema_loader.h
error_handler.o: error_handler.c error_handler.h record.h
statistics.o:    statistics.c statistics.h record.h schema_loader.h
report_writer.o: report_writer.c report_writer.h record.h schema_loader.h \
                 statistics.h error_handler.h

# ── Unit tests ────────────────────────────────────────────────────────────
TEST_SRCS = ../tests/test_c_engine.c protocol.c schema_loader.c validator.c \
            error_handler.c statistics.c report_writer.c
TEST_BIN  = test_engine

ifeq ($(OS),Windows_NT)
TEST_BIN  = test_engine.exe
endif

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): $(TEST_SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
ifeq ($(OS),Windows_NT)
	-$(RM) *.o $(TARGET) $(TEST_BIN) 2>nul
else
	$(RM) *.o $(TARGET) $(TEST_BIN)
endif
