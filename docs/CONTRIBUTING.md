# HACKING

## Build

```sh
make              # normal build
make debug        # with symbols
make asan         # AddressSanitizer
make release      # optimized
```

## Test

```sh
./test_basic.sh           # functional tests
./test_stress.sh 20 60    # 20 clients, 60 seconds
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
ssh_server.c     → SSH protocol, client threads
chat_room.c      → client list, message broadcast
message.c        → persistent storage
tui.c            → terminal rendering
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

- Max 64 clients (MAX_CLIENTS)
- Max 100 messages in memory (MAX_MESSAGES)
- Max 1024 bytes per message (MAX_MESSAGE_LEN)
- Max 64 bytes username (MAX_USERNAME_LEN)

## Common Bugs to Avoid

1. Don't use `strtok()` on client data - use `strtok_r()` or copy first
2. Always increment ref_count before using client outside lock
3. Check SSH API return values (can be SSH_ERROR, SSH_AGAIN, or negative)
4. UTF-8 chars are multi-byte - use utf8_* functions

## Adding Features

1. Add new command in `execute_command()` (ssh_server.c:190)
2. Add new mode in `client_mode_t` enum (common.h:30)
3. Add new vim key in `handle_key()` (ssh_server.c:220)

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
