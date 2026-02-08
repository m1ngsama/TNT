# Unit Tests Makefile
CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -I../../include
LDFLAGS = -pthread

# Source files
UTF8_SRC = ../../src/utf8.c
MESSAGE_SRC = ../../src/message.c

TESTS = test_utf8 test_message

.PHONY: all clean run

all: $(TESTS)

test_utf8: test_utf8.c $(UTF8_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_message: test_message.c $(MESSAGE_SRC) $(UTF8_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

run: all
	@echo "=== Running UTF-8 Tests ==="
	./test_utf8
	@echo ""
	@echo "=== Running Message Tests ==="
	./test_message

clean:
	rm -f $(TESTS) *.o test_messages.log
