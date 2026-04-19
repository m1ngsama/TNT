# Unit Tests Makefile
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_XOPEN_SOURCE=700 -I../../include
LDFLAGS = -pthread

# Detect macOS for _DARWIN_C_SOURCE (needed for timegm)
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
CFLAGS += -D_DARWIN_C_SOURCE
endif

# Source files
UTF8_SRC = ../../src/utf8.c
MESSAGE_SRC = ../../src/message.c
COMMON_SRC = ../../src/common.c
CHAT_ROOM_SRC = ../../src/chat_room.c

TESTS = test_utf8 test_message test_chat_room

.PHONY: all clean run

all: $(TESTS)

test_utf8: test_utf8.c $(UTF8_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_message: test_message.c $(MESSAGE_SRC) $(UTF8_SRC) $(COMMON_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_chat_room: test_chat_room.c $(CHAT_ROOM_SRC) $(MESSAGE_SRC) $(UTF8_SRC) $(COMMON_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

run: all
	@echo "=== Running UTF-8 Tests ==="
	./test_utf8
	@echo ""
	@echo "=== Running Message Tests ==="
	./test_message
	@echo ""
	@echo "=== Running Chat Room Tests ==="
	./test_chat_room

clean:
	rm -f $(TESTS) *.o test_messages.log
