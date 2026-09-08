# TNT - TNT's Not Tunnel
# High-performance terminal chat server written in C

CC ?= cc
CPPFLAGS ?=
CFLAGS ?= -Wall -Wextra -O2 -std=c11
LDFLAGS ?=
LDLIBS ?=
STRIP ?= strip
CPPCHECK ?= cppcheck
CLANG_TIDY ?= clang-tidy
SMALL_CFLAGS ?= -Os -DNDEBUG -ffunction-sections -fdata-sections

ifeq ($(shell uname),Darwin)
SMALL_LDFLAGS ?= -Wl,-dead_strip
else
SMALL_LDFLAGS ?= -Wl,--gc-sections
endif

PROJECT_CPPFLAGS = -D_XOPEN_SOURCE=700 -Iinclude
PROJECT_LDFLAGS =
PROJECT_LDLIBS = -pthread -lssh
BUILD_CFLAGS =
BUILD_LDFLAGS =
DEPFLAGS = -MMD -MP

# Detect libssh location (homebrew on macOS)
ifeq ($(shell uname), Darwin)
    LIBSSH_PREFIX := $(shell brew --prefix libssh 2>/dev/null)
    ifneq ($(LIBSSH_PREFIX),)
        PROJECT_CPPFLAGS += -I$(LIBSSH_PREFIX)/include
        PROJECT_LDFLAGS += -L$(LIBSSH_PREFIX)/lib
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
MAN1DIR ?= $(MANDIR)/man1
MAN5DIR ?= $(MANDIR)/man5
MAN7DIR ?= $(MANDIR)/man7
MAN8DIR ?= $(MANDIR)/man8
BASH_COMPLETION_DIR ?= $(PREFIX)/share/bash-completion/completions
ZSH_COMPLETION_DIR ?= $(PREFIX)/share/zsh/site-functions
FISH_COMPLETION_DIR ?= $(PREFIX)/share/fish/vendor_completions.d
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
PERF_DURABILITY_SECONDS ?= 0
PERF_DURABILITY_INTERVAL ?= 10
PERF_RESOURCE_SAMPLE_INTERVAL ?= 10
PERF_PROGRESS_INTERVAL ?= 60
PERF_SERVER_WRAPPER ?=
PERF_ENFORCE ?= none
PERF_OUTPUT ?=

.PHONY: all clean install install-systemd uninstall uninstall-systemd debug release small release-smoke release-check release-check-strict package-publish-check debian-source-package asan ubsan ubsan-test valgrind check check-cppcheck check-clang-tidy test package-test test-advisory ci-test unit-test script-test integration-test module-runtime-test graceful-shutdown-test login-timeout-test anonymous-access-test connection-limit-test security-test stress-test soak-test slow-client-test user-lifecycle-test perf perf-smoke perf-check perf-full perf-soak perf-soak-smoke info

all: $(TARGETS)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $(PROJECT_LDFLAGS) $(BUILD_LDFLAGS) $(OBJECTS) -o $@ $(PROJECT_LDLIBS) $(LDLIBS)
	@echo "Build complete: $(TARGET)"

$(CTL_TARGET): $(CTL_OBJECTS)
	$(CC) $(LDFLAGS) $(PROJECT_LDFLAGS) $(BUILD_LDFLAGS) $(CTL_OBJECTS) -o $@ $(LDLIBS)
	@echo "Build complete: $(CTL_TARGET)"

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CPPFLAGS) $(PROJECT_CPPFLAGS) $(CFLAGS) $(BUILD_CFLAGS) $(DEPFLAGS) -c $< -o $@

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
	install -d $(DESTDIR)$(MAN1DIR) $(DESTDIR)$(MAN5DIR)
	install -d $(DESTDIR)$(MAN7DIR) $(DESTDIR)$(MAN8DIR)
	install -m 644 tntctl.1 $(DESTDIR)$(MAN1DIR)/
	install -m 644 tnt-message-log.5 $(DESTDIR)$(MAN5DIR)/
	install -m 644 tnt-chat.7 tnt-exec.7 tnt-module-protocol.7 \
		$(DESTDIR)$(MAN7DIR)/
	install -m 644 tnt.8 $(DESTDIR)$(MAN8DIR)/
	install -d $(DESTDIR)$(BASH_COMPLETION_DIR)
	install -m 644 packaging/completions/tntctl.bash \
		$(DESTDIR)$(BASH_COMPLETION_DIR)/tntctl
	install -d $(DESTDIR)$(ZSH_COMPLETION_DIR)
	install -m 644 packaging/completions/_tntctl \
		$(DESTDIR)$(ZSH_COMPLETION_DIR)/_tntctl
	install -d $(DESTDIR)$(FISH_COMPLETION_DIR)
	install -m 644 packaging/completions/tntctl.fish \
		$(DESTDIR)$(FISH_COMPLETION_DIR)/tntctl.fish

install-systemd:
	install -d $(DESTDIR)$(SYSTEMD_UNIT_DIR)
	sed 's#^ExecStart=.*#ExecStart=$(BINDIR)/$(TARGET)#' tnt.service > "$(DESTDIR)$(SYSTEMD_UNIT_DIR)/tnt.service"
	chmod 644 "$(DESTDIR)$(SYSTEMD_UNIT_DIR)/tnt.service"

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	rm -f $(DESTDIR)$(BINDIR)/$(CTL_TARGET)
	rm -f $(DESTDIR)$(MAN1DIR)/tntctl.1
	rm -f $(DESTDIR)$(MAN5DIR)/tnt-message-log.5
	rm -f $(DESTDIR)$(MAN7DIR)/tnt-chat.7
	rm -f $(DESTDIR)$(MAN7DIR)/tnt-exec.7
	rm -f $(DESTDIR)$(MAN7DIR)/tnt-module-protocol.7
	rm -f $(DESTDIR)$(MAN8DIR)/tnt.8
	rm -f $(DESTDIR)$(BASH_COMPLETION_DIR)/tntctl
	rm -f $(DESTDIR)$(ZSH_COMPLETION_DIR)/_tntctl
	rm -f $(DESTDIR)$(FISH_COMPLETION_DIR)/tntctl.fish

uninstall-systemd:
	rm -f $(DESTDIR)$(SYSTEMD_UNIT_DIR)/tnt.service

# Development targets
debug:
	+$(MAKE) clean
	+$(MAKE) BUILD_CFLAGS="-g -DDEBUG" $(TARGETS)

release:
	+$(MAKE) clean
	+$(MAKE) BUILD_CFLAGS="-O3 -DNDEBUG" $(TARGETS)
	$(STRIP) $(TARGET)
	$(STRIP) $(CTL_TARGET)
	+$(MAKE) BUILD_CFLAGS="-O3 -DNDEBUG" release-smoke

small:
	+$(MAKE) clean
	+$(MAKE) BUILD_CFLAGS="$(SMALL_CFLAGS)" \
		BUILD_LDFLAGS="$(SMALL_LDFLAGS)" $(TARGETS)
	$(STRIP) $(TARGET)
	$(STRIP) $(CTL_TARGET)
	+$(MAKE) BUILD_CFLAGS="$(SMALL_CFLAGS)" \
		BUILD_LDFLAGS="$(SMALL_LDFLAGS)" release-smoke

release-smoke: $(TARGETS)
	@version=$$(sed -n 's/^#define TNT_VERSION "\([^"]*\)".*/\1/p' include/common.h); \
	[ -n "$$version" ] || { echo "release-smoke: could not read TNT_VERSION" >&2; exit 1; }; \
	[ "$$(./tnt --version)" = "tnt $$version" ] || { echo "release-smoke: tnt version mismatch" >&2; exit 1; }; \
	[ "$$(./tntctl --version)" = "tntctl $$version" ] || { echo "release-smoke: tntctl version mismatch" >&2; exit 1; }; \
	tmpdir=$$(mktemp -d "$${TMPDIR:-/tmp}/tnt-release-smoke.XXXXXX") || exit 1; \
	trap 'rm -rf "$$tmpdir"' EXIT HUP INT TERM; \
	timestamp=$$(date -u '+%Y-%m-%dT%H:%M:%SZ') || exit 1; \
	printf '%s|smoke|release\n' "$$timestamp" > "$$tmpdir/messages.log"; \
	./tnt --log-check "$$tmpdir/messages.log" > "$$tmpdir/log-check.out" || \
		{ echo "release-smoke: tnt --log-check failed" >&2; exit 1; }; \
	grep -q '^valid_records 1$$' "$$tmpdir/log-check.out" || { echo "release-smoke: log check failed" >&2; exit 1; }; \
	echo "Release smoke passed: tnt $$version"

release-check:
	./scripts/release_check.sh

release-check-strict:
	./scripts/release_check.sh --strict

package-publish-check:
	./scripts/package_publish_check.sh

debian-source-package:
	./scripts/package_debian_source.sh $${OUT_DIR:-dist/debian-source}

asan:
	+$(MAKE) clean
	+$(MAKE) BUILD_CFLAGS="-g -fsanitize=address -fno-omit-frame-pointer" BUILD_LDFLAGS="-fsanitize=address" $(TARGETS)
	@echo "AddressSanitizer build complete. Run with: ASAN_OPTIONS=detect_leaks=1 ./tnt"

ubsan:
	+$(MAKE) clean
	+$(MAKE) BUILD_CFLAGS="-g -fsanitize=undefined -fno-sanitize-recover=undefined" \
		BUILD_LDFLAGS="-fsanitize=undefined" $(TARGETS)

ubsan-test: ubsan
	+$(MAKE) -C tests/unit clean
	+UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
		$(MAKE) -C tests/unit run \
		CFLAGS="-Wall -Wextra -std=c11 -g -fsanitize=undefined -fno-sanitize-recover=undefined" \
		LDFLAGS="-fsanitize=undefined"
	@UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
		$(MAKE) module-runtime-test PORT=$${PORT:-13680} \
		BUILD_CFLAGS="-g -fsanitize=undefined -fno-sanitize-recover=undefined" \
		BUILD_LDFLAGS="-fsanitize=undefined"

valgrind: debug
	@echo "Run: valgrind --leak-check=full --track-origins=yes ./tnt"

# Static analysis
check: check-cppcheck check-clang-tidy

check-cppcheck:
	@command -v "$(CPPCHECK)" >/dev/null 2>&1 || { echo "check: $(CPPCHECK) not found" >&2; exit 127; }; \
	echo "Running cppcheck..."; \
	"$(CPPCHECK)" --enable=warning,performance --error-exitcode=1 --quiet src/

check-clang-tidy:
	@command -v "$(CLANG_TIDY)" >/dev/null 2>&1 || { echo "check: $(CLANG_TIDY) not found" >&2; exit 127; }; \
	echo "Running clang-tidy..."; \
	"$(CLANG_TIDY)" --warnings-as-errors='*' src/*.c -- $(CPPFLAGS) $(PROJECT_CPPFLAGS) $(CFLAGS)

# Test
test: all unit-test script-test integration-test

package-test: all unit-test
	@cd tests && ./test_cli_options.sh
	@cd tests && ./test_manpages.sh
	@cd tests && ./test_completions.sh
	@cd tests && ./test_module_check.sh
	@cd tests && ./test_message_log_tool.sh

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
	@cd tests && ./test_manpages.sh
	@cd tests && ./test_completions.sh
	@cd tests && ./test_get_maintainer.sh
	@cd tests && ./test_check_maintainers.sh
	@cd tests && ./test_module_check.sh
	@cd tests && ./test_install_wizard.sh
	@cd tests && ./test_installer.sh
	@cd tests && ./test_logrotate.sh
	@cd tests && ./test_setup_cron.sh
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
	@cd tests && PORT=$$(($${PORT:-2222} + 9)) ./test_wide_text_view.sh
	@cd tests && PORT=$$(($${PORT:-2222} + 10)) ./test_cursor_editing.sh
	@cd tests && PORT=$$(($${PORT:-2222} + 11)) ./test_default_keymap.sh
	@cd tests && PORT=$$(($${PORT:-2222} + 12)) ./test_vim_keymap.sh
	@cd tests && PORT=$$(($${PORT:-2222} + 13)) ./test_log_migration.sh
	@cd tests && PORT=$$(($${PORT:-2222} + 6)) ./test_module_runtime.sh
	@cd tests && PORT=$$(($${PORT:-2222} + 7)) ./test_graceful_shutdown.sh
	@cd tests && PORT=$$(($${PORT:-2222} + 8)) ./test_login_timeouts.sh
	@cd tests && ./test_tntctl_cli.sh

module-runtime-test: all
	@echo "Running module runtime tests..."
	@cd tests && PORT=$${PORT:-2222} ./test_module_runtime.sh

graceful-shutdown-test: all
	@echo "Running graceful shutdown tests..."
	@cd tests && PORT=$${PORT:-2222} ./test_graceful_shutdown.sh

login-timeout-test: all
	@echo "Running login timeout tests..."
	@cd tests && PORT=$${PORT:-2222} ./test_login_timeouts.sh

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
		--durability-seconds "$(PERF_DURABILITY_SECONDS)" \
		--durability-interval "$(PERF_DURABILITY_INTERVAL)" \
		--resource-sample-interval "$(PERF_RESOURCE_SAMPLE_INTERVAL)" \
		--progress-interval "$(PERF_PROGRESS_INTERVAL)" \
		$(if $(strip $(PERF_SERVER_WRAPPER)),--server-wrapper "$(PERF_SERVER_WRAPPER)",) \
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
# 101 samples make nearest-rank p99 the second-highest observation rather than
# relabeling one maximum as a percentile.
perf-full: PERF_CLIENTS = 64
perf-full: PERF_STORM_CLIENTS = 64
perf-full: PERF_FANOUT_SAMPLES = 101
perf-full: PERF_IDLE_SECONDS = 10
perf-full: PERF_MESSAGES = 1000
perf-full: PERF_ENFORCE = all
perf-full: perf

# Optional durability phase reuses the full profile's already joined sessions.
perf-soak: PERF_CLIENTS = 64
perf-soak: PERF_STORM_CLIENTS = 64
perf-soak: PERF_FANOUT_SAMPLES = 101
perf-soak: PERF_IDLE_SECONDS = 10
perf-soak: PERF_MESSAGES = 1000
perf-soak: PERF_DURABILITY_SECONDS = 1800
perf-soak: PERF_ENFORCE = all
perf-soak: perf

perf-soak-smoke: PERF_STARTUP_SAMPLES = 5
perf-soak-smoke: PERF_HANDSHAKE_SAMPLES = 5
perf-soak-smoke: PERF_FANOUT_SAMPLES = 5
perf-soak-smoke: PERF_STORM_CLIENTS = 5
perf-soak-smoke: PERF_CLIENTS = 5
perf-soak-smoke: PERF_IDLE_SECONDS = 0.1
perf-soak-smoke: PERF_MESSAGES = 5
perf-soak-smoke: PERF_HISTORY_RECORDS = 10
perf-soak-smoke: PERF_SLOW_CLIENT_MESSAGES = 1
perf-soak-smoke: PERF_DURABILITY_SECONDS = 2
perf-soak-smoke: PERF_DURABILITY_INTERVAL = 0.2
perf-soak-smoke: PERF_RESOURCE_SAMPLE_INTERVAL = 0.2
perf-soak-smoke: PERF_PROGRESS_INTERVAL = 0
perf-soak-smoke: PERF_ENFORCE = none
perf-soak-smoke: perf

ci-test:
	@$(MAKE) test PORT=$(CI_TEST_PORT)
	@$(MAKE) anonymous-access-test PORT=$$(($(CI_TEST_PORT) + 5))
	@$(MAKE) connection-limit-test PORT=$$(($(CI_TEST_PORT) + 10))
	@$(MAKE) security-test PORT=$$(($(CI_TEST_PORT) + 20))

# Show build info
info:
	@echo "Compiler: $(CC)"
	@echo "CPPFLAGS: $(CPPFLAGS) $(PROJECT_CPPFLAGS)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "LDFLAGS: $(LDFLAGS) $(PROJECT_LDFLAGS)"
	@echo "LDLIBS: $(PROJECT_LDLIBS) $(LDLIBS)"
	@echo "Sources: $(SOURCES)"
	@echo "Objects: $(OBJECTS)"

-include $(DEPS)
