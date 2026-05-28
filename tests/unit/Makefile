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
MESSAGE_LOG_SRC = ../../src/message_log.c
COMMON_SRC = ../../src/common.c
CONFIG_DEFAULTS_SRC = ../../src/config_defaults.c
COMMAND_CATALOG_SRC = ../../src/command_catalog.c
CLI_TEXT_SRC = ../../src/cli_text.c
TNTCTL_TEXT_SRC = ../../src/tntctl_text.c
CHAT_ROOM_SRC = ../../src/chat_room.c
HISTORY_VIEW_SRC = ../../src/history_view.c
I18N_SRC = ../../src/i18n.c
I18N_TEXT_SRC = ../../src/i18n_text.c
EXEC_CATALOG_SRC = ../../src/exec_catalog.c
SYSTEM_MESSAGE_SRC = ../../src/system_message.c
HELP_TEXT_SRC = ../../src/help_text.c
MANUAL_TEXT_SRC = ../../src/manual_text.c
RATELIMIT_SRC = ../../src/ratelimit.c

TESTS = test_utf8 test_message test_chat_room test_history_view test_i18n test_system_message test_command_catalog test_exec_catalog test_help_text test_manual_text test_cli_text test_tntctl_text test_ratelimit test_config_defaults

.PHONY: all clean run

all: $(TESTS)

test_utf8: test_utf8.c $(UTF8_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_message: test_message.c $(MESSAGE_SRC) $(MESSAGE_LOG_SRC) $(UTF8_SRC) $(COMMON_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_chat_room: test_chat_room.c $(CHAT_ROOM_SRC) $(MESSAGE_SRC) $(MESSAGE_LOG_SRC) $(UTF8_SRC) $(COMMON_SRC) $(CONFIG_DEFAULTS_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_history_view: test_history_view.c $(HISTORY_VIEW_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_i18n: test_i18n.c $(I18N_SRC) $(I18N_TEXT_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_system_message: test_system_message.c $(SYSTEM_MESSAGE_SRC) $(I18N_SRC) $(I18N_TEXT_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_command_catalog: test_command_catalog.c $(COMMAND_CATALOG_SRC) $(COMMON_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_exec_catalog: test_exec_catalog.c $(EXEC_CATALOG_SRC) $(COMMON_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_help_text: test_help_text.c $(HELP_TEXT_SRC) $(COMMAND_CATALOG_SRC) $(COMMON_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_manual_text: test_manual_text.c $(MANUAL_TEXT_SRC) $(COMMAND_CATALOG_SRC) $(COMMON_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_cli_text: test_cli_text.c $(CLI_TEXT_SRC) $(COMMON_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_tntctl_text: test_tntctl_text.c $(TNTCTL_TEXT_SRC) $(EXEC_CATALOG_SRC) $(COMMON_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_ratelimit: test_ratelimit.c $(RATELIMIT_SRC) $(COMMON_SRC) $(CONFIG_DEFAULTS_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

test_config_defaults: test_config_defaults.c $(CONFIG_DEFAULTS_SRC) $(COMMON_SRC)
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
	@echo ""
	@echo "=== Running History View Tests ==="
	./test_history_view
	@echo ""
	@echo "=== Running i18n Tests ==="
	./test_i18n
	@echo ""
	@echo "=== Running System Message Tests ==="
	./test_system_message
	@echo ""
	@echo "=== Running Command Catalog Tests ==="
	./test_command_catalog
	@echo ""
	@echo "=== Running Exec Catalog Tests ==="
	./test_exec_catalog
	@echo ""
	@echo "=== Running Help Text Tests ==="
	./test_help_text
	@echo ""
	@echo "=== Running Manual Text Tests ==="
	./test_manual_text
	@echo ""
	@echo "=== Running CLI Text Tests ==="
	./test_cli_text
	@echo ""
	@echo "=== Running tntctl Text Tests ==="
	./test_tntctl_text
	@echo ""
	@echo "=== Running Rate Limit Tests ==="
	./test_ratelimit
	@echo ""
	@echo "=== Running Config Defaults Tests ==="
	./test_config_defaults

clean:
	rm -f $(TESTS) *.o test_messages.log
