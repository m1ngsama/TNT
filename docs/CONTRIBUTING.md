# HACKING

## Build

```sh
make                    # normal build
make debug              # with symbols
make asan               # AddressSanitizer
make release            # optimized
make release-check      # release preflight
```

## Test

```sh
make test                 # unit + integration tests
make ci-test              # local CI-equivalent checks
make stress-test          # concurrent-client stress test
make soak-test            # idle/reconnect/control-plane soak
make slow-client-test     # slow interactive-client backpressure
make user-lifecycle-test  # two-user TUI lifecycle
```

## Debug

```sh
# Memory leaks
ASAN_OPTIONS=detect_leaks=1 ./tnt

# Or use valgrind
make valgrind
valgrind --leak-check=full ./tnt

# Static analysis
make check
```

## Architecture

```
main.c           → entry point, signal handling
cli_text.c       → startup CLI text
tntctl_text.c    → tntctl local help and diagnostics
command_catalog.c → COMMAND-mode command metadata, usage, and argument shape
commands.c       → COMMAND-mode command dispatch
exec_catalog.c   → SSH exec command matching, usage, and argument shape
exec.c           → SSH exec command dispatch
tntctl.c         → local wrapper around the SSH exec interface
ssh_server.c     → SSH listener setup
bootstrap.c      → SSH authentication/session bootstrap
input.c          → interactive session loop
chat_room.c      → client list, message broadcast
history_view.c   → message viewport and scroll state
i18n.c           → UI language and locale selection
i18n_text.c      → shared UI text catalog
message.c        → persistent storage
tui.c            → terminal rendering
tui_status.c     → status/input-line rendering
utf8.c           → UTF-8 string handling
```

## Thread Safety

- `g_room->lock`: RWlock for client list and messages
- `client->ref_lock`: Mutex for reference counting
- Each client runs in detached thread
- Reference counting prevents use-after-free

## Memory Management

- Clients: ref-counted, freed when ref==0
- Messages: fixed ring buffer (MAX_MESSAGES)
- No dynamic string allocation

## Known Limits

- Default 64 clients, configurable with `TNT_MAX_CONNECTIONS`
- Max 100 messages in memory (MAX_MESSAGES)
- Max 1024 bytes per message (MAX_MESSAGE_LEN)
- Max 64 bytes username (MAX_USERNAME_LEN)

## Common Bugs to Avoid

1. Don't use `strtok()` on client data - use `strtok_r()` or copy first
2. Always use `client_addref()` / `client_release()` before using a client
   outside `g_room->lock`; never modify `ref_count` directly
3. Check SSH API return values (can be SSH_ERROR, SSH_AGAIN, or negative)
4. UTF-8 chars are multi-byte - use utf8_* functions

## Adding Features

1. Add interactive command metadata, usage text, and argument shape in
   `src/command_catalog.c`.
2. Add interactive command behavior in `src/commands.c`.
3. Add SSH exec metadata in `src/exec_catalog.c` and dispatch in `src/exec.c`
   only when the feature should be scriptable.
4. Put shared localized strings in `src/i18n_text.c`.
5. Add or update the narrowest unit/integration test for the behavior.

## Adding Modules

TNT core owns the module protocol, runtime supervisor, and compatibility tests.
Community modules and module examples live in the companion repository:

```text
https://github.com/m1ngsama/tnt-modules
```

For core protocol or runtime changes, update `docs/MODULE_PROTOCOL.md`, add or
update tests in this repository, and keep `scripts/module_check.sh` aligned with
the manifest and handshake rules. For new module implementations, contribute to
the companion module repository instead of adding them to TNT core.

## Debugging Tips

```sh
# Find memory issues
make asan
ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 ./tnt

# Check thread issues
gcc -fsanitize=thread ...

# Profile
gcc -pg ...
./tnt
gprof tnt
```
