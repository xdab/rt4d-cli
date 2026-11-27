# rt4d-cli: one binary, one job per verb, nothing beyond libc and POSIX.
CC ?= cc
# POSIX 2008 exposes poll(), clock_gettime() and the termios calls the
# transport needs; plain -std=c11 hides them behind a feature-test macro
CFLAGS := -std=c11 -O2 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L -I./include
LDLIBS :=

SRC_DIR := src
OBJ_DIR := obj
BIN_DIR := bin
TARGET := $(BIN_DIR)/rt4d-cli

SOURCES := $(wildcard $(SRC_DIR)/*.c)
OBJECTS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SOURCES))
# every C/h file `make fmt` and `make fmt-check` own
C_SOURCES := $(SOURCES) $(wildcard include/*.h)
# the Python tests, formatted and linted by ruff (ruff.toml)
PY_SOURCES := $(wildcard tests/*.py)

.PHONY: all
all: $(TARGET)

$(TARGET): $(OBJECTS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(OBJECTS) $(LDLIBS)
	@echo "✓ Build successful: $@"

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(OBJ_DIR) $(BIN_DIR):
	mkdir -p $@

-include $(OBJECTS:.o=.d)

# clangd and clang-tidy read compile_commands.json; regenerate after adding
# sources or changing CFLAGS, then restart the language server
COMPDB := compile_commands.json

.PHONY: compdb
compdb:
	@printf '[\n' > $(COMPDB)
	@n=0; for src in $(SOURCES); do \
		n=$$((n + 1)); \
		[ $$n -eq 1 ] || printf ',\n' >> $(COMPDB); \
		{ printf '  {"directory": "%s", "file": "%s", "arguments": ["%s"' \
			"$(CURDIR)" "$$src" "$(CC)"; \
		  for fl in $(CFLAGS); do printf ', "%s"' "$$fl"; done; \
		  printf ', "-c", "%s"]}' "$$src"; } >> $(COMPDB); \
	done; printf '\n]\n' >> $(COMPDB)
	@echo "✓ $(COMPDB): $(words $(SOURCES)) sources"

# clang-format for C/H; fmt-check is its gate twin (nonzero on any difference)
.PHONY: fmt fmt-check
fmt:
	@command -v clang-format >/dev/null 2>&1 || { \
		echo "ERROR: clang-format not installed (Arch: sudo pacman -S clang)"; exit 1; }
	@command -v ruff >/dev/null 2>&1 || { \
		echo "ERROR: ruff not installed (Arch: sudo pacman -S ruff)"; exit 1; }
	clang-format -i $(C_SOURCES)
	ruff format $(PY_SOURCES)
	ruff check --fix $(PY_SOURCES)

fmt-check:
	@command -v clang-format >/dev/null 2>&1 || { \
		echo "ERROR: clang-format not installed (Arch: sudo pacman -S clang)"; exit 1; }
	@command -v ruff >/dev/null 2>&1 || { \
		echo "ERROR: ruff not installed (Arch: sudo pacman -S ruff)"; exit 1; }
	clang-format --dry-run --Werror $(C_SOURCES)
	ruff format --check $(PY_SOURCES)
	ruff check $(PY_SOURCES)

# clang-tidy gate (checks in .clang-tidy)
.PHONY: tidy
tidy: compdb
	@command -v clang-tidy >/dev/null 2>&1 || { \
		echo "ERROR: clang-tidy not installed (Arch: sudo pacman -S clang-tidy)"; exit 1; }
	clang-tidy -p . $(SOURCES) --warnings-as-errors=*

# tests over a modelled radio: no serial device, no root, no recorded dump
.PHONY: test
test: $(TARGET)
	python3 -m unittest discover -s tests -v

.PHONY: clean
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR) $(COMPDB)
	@echo "✓ Clean complete"

.PHONY: help
help:
	@echo "rt4d-cli Makefile Targets"
	@echo "========================="
	@echo "  all        - Build $(TARGET) (default)"
	@echo "  compdb     - Regenerate $(COMPDB) (editor and clang-tidy input)"
	@echo "  fmt        - Format src/*.c, include/*.h (clang-format) and tests/*.py (ruff)"
	@echo "  fmt-check  - Check formatting and lint (gate, nonzero on any finding)"
	@echo "  tidy       - Static analysis gate over src/ (clang-tidy, .clang-tidy)"
	@echo "  test       - Run the restore tests against a modelled radio"
	@echo "  clean      - Remove build artifacts (obj/, bin/, $(COMPDB))"
	@echo ""
	@echo "Usage:"
	@echo "  make             # build bin/rt4d-cli"
	@echo "  make fmt         # before committing"
	@echo "  make clean       # start over"
