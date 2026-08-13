# TNT - TNT's Not Tunnel
# High-performance terminal chat server written in C

CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11 -D_XOPEN_SOURCE=700
LDFLAGS = -pthread -lssh
CTL_LDFLAGS =
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

SOURCES = $(filter-out $(SRC_DIR)/tntctl.c $(SRC_DIR)/tntctl_text.c,$(wildcard $(SRC_DIR)/*.c))
OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
DEPS = $(OBJECTS:.o=.d) $(CTL_OBJECTS:.o=.d)
TARGET = tnt
CTL_TARGET = tntctl
CTL_OBJECTS = $(OBJ_DIR)/tntctl.o $(OBJ_DIR)/tntctl_text.o $(OBJ_DIR)/exec_catalog.o $(OBJ_DIR)/common.o $(OBJ_DIR)/config_defaults.o $(OBJ_DIR)/i18n.o
TARGETS = $(TARGET) $(CTL_TARGET)

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
MANDIR ?= $(PREFIX)/share/man
SYSTEMD_UNIT_DIR ?= $(PREFIX)/lib/systemd/system
CI_TEST_PORT ?= $(if $(PORT),$(PORT),2222)

PERF_STARTUP_SAMPLES ?= 21
PERF_HANDSHAKE_SAMPLES ?= 11
PERF_FANOUT_SAMPLES ?= 11
PERF_STORM_CLIENTS ?= 8
PERF_CLIENTS ?= 8
PERF_IDLE_SECONDS ?= 2
PERF_MESSAGES ?= 100
PERF_HISTORY_RECORDS ?= 100000
PERF_SLOW_CLIENT_SAMPLES ?= 5
PERF_SLOW_CLIENT_CHARACTERS ?= 3200
PERF_SLOW_CLIENT_MESSAGES ?= 16
PERF_MODULE_PATHS ?=
PERF_MODULE_CLIENTS ?= 8
PERF_MODULE_MESSAGES ?= 100
PERF_SERVER_WRAPPER ?=
PERF_ENFORCE ?= none
PERF_OUTPUT ?=

PERF_SOAK_CLIENTS ?= 64
PERF_SOAK_DURATION ?= 1800
PERF_SOAK_WARMUP ?= 1
PERF_SOAK_MESSAGE_INTERVAL ?= 10
PERF_SOAK_SAMPLE_INTERVAL ?= 10
PERF_SOAK_PROGRESS_INTERVAL ?= 60
PERF_SOAK_SERVER_WRAPPER ?=
PERF_SOAK_OUTPUT ?=

.PHONY: all clean install install-systemd uninstall uninstall-systemd debug release release-check release-check-strict package-publish-check debian-source-package asan valgrind check test test-advisory ci-test unit-test script-test integration-test module-runtime-test graceful-shutdown-test anonymous-access-test connection-limit-test security-test stress-test soak-test slow-client-test user-lifecycle-test perf perf-smoke perf-check perf-full perf-soak perf-soak-smoke info

all: $(TARGETS)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)
	@echo "Build complete: $(TARGET)"

$(CTL_TARGET): $(CTL_OBJECTS)
	$(CC) $(CTL_OBJECTS) -o $@ $(CTL_LDFLAGS)
	@echo "Build complete: $(CTL_TARGET)"

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(DEPFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(TARGETS)
	rm -f tests/*.log tests/host_key* tests/messages.log
	@echo "Clean complete"

install: $(TARGETS)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/
	install -m 755 $(CTL_TARGET) $(DESTDIR)$(BINDIR)/
	install -d $(DESTDIR)$(MANDIR)/man1
	install -m 644 tnt.1 $(DESTDIR)$(MANDIR)/man1/
	install -m 644 tntctl.1 $(DESTDIR)$(MANDIR)/man1/

install-systemd:
	install -d $(DESTDIR)$(SYSTEMD_UNIT_DIR)
	sed 's#^ExecStart=.*#ExecStart=$(BINDIR)/$(TARGET)#' tnt.service > "$(DESTDIR)$(SYSTEMD_UNIT_DIR)/tnt.service"
	chmod 644 "$(DESTDIR)$(SYSTEMD_UNIT_DIR)/tnt.service"

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -f $(DESTDIR)$(BINDIR)/$(CTL_TARGET)
	rm -f $(DESTDIR)$(MANDIR)/man1/tnt.1
	rm -f $(DESTDIR)$(MANDIR)/man1/tntctl.1

uninstall-systemd:
	rm -f $(DESTDIR)$(SYSTEMD_UNIT_DIR)/tnt.service

# Development targets
debug: CFLAGS += -g -DDEBUG
debug: clean $(TARGETS)

release: CFLAGS += -O3 -DNDEBUG
release: clean $(TARGETS)
	strip $(TARGET)
	strip $(CTL_TARGET)

release-check:
	./scripts/release_check.sh

release-check-strict:
	./scripts/release_check.sh --strict

package-publish-check:
	./scripts/package_publish_check.sh

debian-source-package:
	./scripts/package_debian_source.sh $${OUT_DIR:-dist/debian-source}

asan: CFLAGS += -g -fsanitize=address -fno-omit-frame-pointer
asan: LDFLAGS += -fsanitize=address
asan: CTL_LDFLAGS += -fsanitize=address
asan: clean $(TARGETS)
	@echo "AddressSanitizer build complete. Run with: ASAN_OPTIONS=detect_leaks=1 ./tnt"

valgrind: debug
	@echo "Run: valgrind --leak-check=full --track-origins=yes ./tnt"

# Static analysis
check:
	@command -v cppcheck >/dev/null 2>&1 && cppcheck --enable=warning,performance --quiet src/ || echo "cppcheck not installed"
	@command -v clang-tidy >/dev/null 2>&1 && clang-tidy src/*.c -- -Iinclude $(INCLUDES) || echo "clang-tidy not installed"

# Test
test: all unit-test script-test integration-test

test-advisory: all unit-test
	@echo "Running integration tests..."
	@cd tests && PORT=$${PORT:-2222} ./test_basic.sh || echo "(basic integration tests are advisory)"
	@cd tests && PORT=$$(($${PORT:-2222} + 1)) ./test_exec_mode.sh || echo "(exec mode tests are advisory)"
	@cd tests && PORT=$$(($${PORT:-2222} + 2)) ./test_interactive_input.sh || echo "(interactive input tests are advisory)"
	@cd tests && PORT=$$(($${PORT:-2222} + 6)) ./test_module_runtime.sh || echo "(module runtime tests are advisory)"

unit-test:
	@echo "Running unit tests..."
	@$(MAKE) -C tests/unit run

script-test: all
	@echo "Running script tests..."
	@cd tests && ./test_cli_options.sh
	@cd tests && ./test_docs_help_surface.sh
	@cd tests && ./test_get_maintainer.sh
	@cd tests && ./test_check_maintainers.sh
	@cd tests && ./test_module_check.sh
	@cd tests && ./test_install_wizard.sh
	@cd tests && ./test_logrotate.sh
	@cd tests && ./test_message_log_tool.sh
	@cd tests && ./test_source_archive.sh
	@cd tests && ./test_release_artifact_gate.sh
	@cd tests && ./test_perf_benchmark.sh

integration-test: all
	@echo "Running integration tests..."
	@cd tests && PORT=$${PORT:-2222} ./test_basic.sh
	@cd tests && PORT=$$(($${PORT:-2222} + 1)) ./test_exec_mode.sh
	@cd tests && PORT=$$(($${PORT:-2222} + 2)) ./test_interactive_input.sh
	@cd tests && PORT=$$(($${PORT:-2222} + 3)) ./test_user_lifecycle.sh
	@cd tests && PORT=$$(($${PORT:-2222} + 4)) ./test_mute_joins_view.sh
	@cd tests && PORT=$$(($${PORT:-2222} + 5)) ./test_empty_view.sh
	@cd tests && PORT=$$(($${PORT:-2222} + 6)) ./test_module_runtime.sh
	@cd tests && PORT=$$(($${PORT:-2222} + 7)) ./test_graceful_shutdown.sh
	@cd tests && ./test_tntctl_cli.sh

module-runtime-test: all
	@echo "Running module runtime tests..."
	@cd tests && PORT=$${PORT:-2222} ./test_module_runtime.sh

graceful-shutdown-test: all
	@echo "Running graceful shutdown tests..."
	@cd tests && PORT=$${PORT:-2222} ./test_graceful_shutdown.sh

anonymous-access-test: all
	@echo "Running anonymous access tests..."
	@cd tests && PORT=$${PORT:-2222} ./test_anonymous_access.sh

connection-limit-test: all
	@echo "Running connection limit tests..."
	@cd tests && PORT=$${PORT:-2222} ./test_connection_limits.sh

security-test: all
	@echo "Running security feature tests..."
	@cd tests && PORT=$${PORT:-13600} ./test_security_features.sh

stress-test: all
	@echo "Running stress tests..."
	@cd tests && PORT=$${PORT:-2222} ./test_stress.sh $${CLIENTS:-10} $${DURATION:-30}

soak-test: all
	@echo "Running soak tests..."
	@cd tests && PORT=$${PORT:-2222} ./test_soak.sh $${DURATION:-8} $${RECONNECTS:-5}

slow-client-test: all
	@echo "Running slow-client tests..."
	@cd tests && PORT=$${PORT:-2222} ./test_slow_client.sh $${DURATION:-8} $${BURST_CHARS:-1600}

user-lifecycle-test: all
	@echo "Running user lifecycle tests..."
	@cd tests && PORT=$${PORT:-2222} ./test_user_lifecycle.sh

# Reproducible real-client performance benchmark. Results are JSON and are
# ignored by git under perf-results/ unless PERF_OUTPUT names another path.
perf: all
	@./scripts/perf_benchmark.py \
		--binary ./tnt \
		--startup-samples "$(PERF_STARTUP_SAMPLES)" \
		--handshake-samples "$(PERF_HANDSHAKE_SAMPLES)" \
		--fanout-samples "$(PERF_FANOUT_SAMPLES)" \
		--storm-clients "$(PERF_STORM_CLIENTS)" \
		--clients "$(PERF_CLIENTS)" \
		--idle-seconds "$(PERF_IDLE_SECONDS)" \
		--messages "$(PERF_MESSAGES)" \
		--history-records "$(PERF_HISTORY_RECORDS)" \
		--slow-client-samples "$(PERF_SLOW_CLIENT_SAMPLES)" \
		--slow-client-characters "$(PERF_SLOW_CLIENT_CHARACTERS)" \
		--slow-client-messages "$(PERF_SLOW_CLIENT_MESSAGES)" \
		--module-clients "$(PERF_MODULE_CLIENTS)" \
		--module-messages "$(PERF_MODULE_MESSAGES)" \
		$(if $(strip $(PERF_SERVER_WRAPPER)),--server-wrapper "$(PERF_SERVER_WRAPPER)",) \
		$(if $(strip $(PERF_MODULE_PATHS)),--module-paths "$(PERF_MODULE_PATHS)",) \
		$(if $(strip $(PERF_OUTPUT)),--output "$(PERF_OUTPUT)",) \
		--enforce "$(PERF_ENFORCE)"

# Short CI workload; 21 startup samples keep nearest-rank p95 from collapsing
# to a single worst sample while the non-gating scenarios remain compact.
perf-smoke: PERF_STARTUP_SAMPLES = 21
perf-smoke: PERF_HANDSHAKE_SAMPLES = 11
perf-smoke: PERF_FANOUT_SAMPLES = 5
perf-smoke: PERF_STORM_CLIENTS = 5
perf-smoke: PERF_CLIENTS = 2
perf-smoke: PERF_IDLE_SECONDS = 1
perf-smoke: PERF_MESSAGES = 20
perf-smoke: PERF_HISTORY_RECORDS = 1000
perf-smoke: PERF_SLOW_CLIENT_MESSAGES = 4
perf-smoke: PERF_ENFORCE = stable
perf-smoke: perf

# Enforce the low-variance startup, idle RSS, and binary-size gates.
perf-check: PERF_ENFORCE = stable
perf-check: perf

# Exercise and enforce the issue #66 target concurrency and all-receiver load.
perf-full: PERF_CLIENTS = 64
perf-full: PERF_STORM_CLIENTS = 64
perf-full: PERF_IDLE_SECONDS = 10
perf-full: PERF_MESSAGES = 1000
perf-full: PERF_ENFORCE = all
perf-full: perf

# Long-duration functional contract for issue #66. Every one of 64 real
# sessions sends, every marker must persist in order and render everywhere,
# and RSS/virtual memory are sampled for the full 30-minute default duration.
perf-soak: all
	@./scripts/perf_soak.py \
		--binary ./tnt \
		--clients "$(PERF_SOAK_CLIENTS)" \
		--duration "$(PERF_SOAK_DURATION)" \
		--warmup "$(PERF_SOAK_WARMUP)" \
		--message-interval "$(PERF_SOAK_MESSAGE_INTERVAL)" \
		--sample-interval "$(PERF_SOAK_SAMPLE_INTERVAL)" \
		--progress-interval "$(PERF_SOAK_PROGRESS_INTERVAL)" \
		$(if $(strip $(PERF_SOAK_SERVER_WRAPPER)),--server-wrapper "$(PERF_SOAK_SERVER_WRAPPER)",) \
		$(if $(strip $(PERF_SOAK_OUTPUT)),--output "$(PERF_SOAK_OUTPUT)",)

perf-soak-smoke: PERF_SOAK_CLIENTS = 5
perf-soak-smoke: PERF_SOAK_DURATION = 2
perf-soak-smoke: PERF_SOAK_WARMUP = 0.1
perf-soak-smoke: PERF_SOAK_MESSAGE_INTERVAL = 0.2
perf-soak-smoke: PERF_SOAK_SAMPLE_INTERVAL = 0.2
perf-soak-smoke: PERF_SOAK_PROGRESS_INTERVAL = 0
perf-soak-smoke: perf-soak

ci-test:
	@$(MAKE) test PORT=$(CI_TEST_PORT)
	@$(MAKE) anonymous-access-test PORT=$$(($(CI_TEST_PORT) + 5))
	@$(MAKE) connection-limit-test PORT=$$(($(CI_TEST_PORT) + 10))
	@$(MAKE) security-test PORT=$$(($(CI_TEST_PORT) + 20))

# Show build info
info:
	@echo "Compiler: $(CC)"
	@echo "Flags: $(CFLAGS)"
	@echo "Sources: $(SOURCES)"
	@echo "Objects: $(OBJECTS)"

-include $(DEPS)
