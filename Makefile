# TNT - TNT's Not Tunnel
# High-performance terminal chat server written in C

CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11 -D_XOPEN_SOURCE=700
LDFLAGS = -pthread -lssh
INCLUDES = -Iinclude

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
TARGET = tnt

.PHONY: all clean install uninstall debug release asan valgrind check info

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)
	@echo "Build complete: $(TARGET)"

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(TARGET)
	@echo "Clean complete"

install: $(TARGET)
	install -d $(DESTDIR)/usr/local/bin
	install -m 755 $(TARGET) $(DESTDIR)/usr/local/bin/

uninstall:
	rm -f $(DESTDIR)/usr/local/bin/$(TARGET)

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

# Show build info
info:
	@echo "Compiler: $(CC)"
	@echo "Flags: $(CFLAGS)"
	@echo "Sources: $(SOURCES)"
	@echo "Objects: $(OBJECTS)"
