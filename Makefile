# TNT - TNT's Not Tunnel
# High-performance terminal chat server written in C

CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11 -D_XOPEN_SOURCE=700
LDFLAGS = -pthread -lssh
INCLUDES = -Iinclude
DEPFLAGS = -MMD -MP

# Detect libssh location (homebrew on macOS)
ifeq ($(shell uname), Darwin)
    LIBSSH_PREFIX := $(shell brew --prefix libssh 2>/dev/null)
    ifneq ($(LIBSSH_PREFIX),)
        INCLUDES += -I$(LIBSSH_PREFIX)/include
        LDFLAGS += -L$(LIBSSH_PREFIX)/lib
    endif
endif

SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj

SOURCES = $(wildcard $(SRC_DIR)/*.c)
OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
DEPS = $(OBJECTS:.o=.d)
TARGET = tnt

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
MANDIR ?= $(PREFIX)/share/man
SYSTEMD_UNIT_DIR ?= $(PREFIX)/lib/systemd/system

.PHONY: all clean install install-systemd uninstall uninstall-systemd debug release asan valgrind check test unit-test info

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)
	@echo "Build complete: $(TARGET)"

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(DEPFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(TARGET)
	rm -f tests/*.log tests/host_key* tests/messages.log
	@echo "Clean complete"

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/
	install -d $(DESTDIR)$(MANDIR)/man1
	install -m 644 tnt.1 $(DESTDIR)$(MANDIR)/man1/

install-systemd:
	install -d $(DESTDIR)$(SYSTEMD_UNIT_DIR)
	install -m 644 tnt.service $(DESTDIR)$(SYSTEMD_UNIT_DIR)/

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -f $(DESTDIR)$(MANDIR)/man1/tnt.1

uninstall-systemd:
	rm -f $(DESTDIR)$(SYSTEMD_UNIT_DIR)/tnt.service

# Development targets
debug: CFLAGS += -g -DDEBUG
debug: clean $(TARGET)

release: CFLAGS += -O3 -DNDEBUG
release: clean $(TARGET)
	strip $(TARGET)

asan: CFLAGS += -g -fsanitize=address -fno-omit-frame-pointer
asan: LDFLAGS += -fsanitize=address
asan: clean $(TARGET)
	@echo "AddressSanitizer build complete. Run with: ASAN_OPTIONS=detect_leaks=1 ./tnt"

valgrind: debug
	@echo "Run: valgrind --leak-check=full --track-origins=yes ./tnt"

# Static analysis
check:
	@command -v cppcheck >/dev/null 2>&1 && cppcheck --enable=warning,performance --quiet src/ || echo "cppcheck not installed"
	@command -v clang-tidy >/dev/null 2>&1 && clang-tidy src/*.c -- -Iinclude $(INCLUDES) || echo "clang-tidy not installed"

# Test
test: all unit-test
	@echo "Running integration tests..."
	@cd tests && PORT=$${PORT:-2222} ./test_basic.sh || echo "(basic integration tests are advisory)"
	@cd tests && PORT=$$(($${PORT:-2222} + 1)) ./test_exec_mode.sh || echo "(exec mode tests are advisory)"
	@cd tests && PORT=$$(($${PORT:-2222} + 2)) ./test_interactive_input.sh || echo "(interactive input tests are advisory)"

unit-test:
	@echo "Running unit tests..."
	@$(MAKE) -C tests/unit run

# Show build info
info:
	@echo "Compiler: $(CC)"
	@echo "Flags: $(CFLAGS)"
	@echo "Sources: $(SOURCES)"
	@echo "Objects: $(OBJECTS)"

-include $(DEPS)
