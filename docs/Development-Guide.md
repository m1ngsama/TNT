# TNT Development Guide

Complete guide for TNT developers and contributors.

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Code Structure](#code-structure)
3. [Building and Testing](#building-and-testing)
4. [Core Components](#core-components)
5. [Adding Features](#adding-features)
6. [Debugging](#debugging)
7. [Performance Optimization](#performance-optimization)
8. [Contributing Guidelines](#contributing-guidelines)

---

## Architecture Overview

TNT uses a multi-threaded architecture with a main accept loop and per-client threads.

```
┌─────────────────────────────────────────────────────────┐
│                    Main Thread                          │
│  ┌──────────────────────────────────────────────────┐  │
│  │  ssh_server_start()                              │  │
│  │    └─> ssh_bind_accept()                         │  │
│  │        └─> Event loop (auth + channel setup)     │  │
│  │            └─> pthread_create(client_thread)     │  │
│  └──────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
                          │
        ┌─────────────────┴─────────────────┐
        │                                   │
┌───────▼────────┐                 ┌────────▼───────┐
│ Client Thread 1│                 │ Client Thread N│
│  ┌──────────┐  │                 │  ┌──────────┐  │
│  │ Session  │  │      ...        │  │ Session  │  │
│  │ Handler  │  │                 │  │ Handler  │  │
│  └──────────┘  │                 │  └──────────┘  │
└────────────────┘                 └────────────────┘
        │                                   │
        └───────────┬───────────────────────┘
                    │
         ┌──────────▼──────────┐
         │   Chat Room         │
         │  ┌──────────────┐   │
         │  │ RW Lock      │   │
         │  │ Clients[]    │   │
         │  │ Messages[]   │   │
         │  └──────────────┘   │
         └─────────────────────┘
```

### Key Design Principles

1. **Fixed-size buffers** - No dynamic allocation in hot paths
2. **Reader-writer locks** - Multiple readers, single writer
3. **Reference counting** - Prevent use-after-free
4. **Ring buffer** - Fixed-size message history (last 100 messages)

---

## Code Structure

### Source Files

```
src/
├── main.c           - Entry point, signal handling
├── ssh_server.c     - SSH server, client threads, authentication
├── chat_room.c      - Chat room logic, message broadcasting
├── message.c        - Message persistence (RFC3339 format)
├── tui.c            - Terminal UI rendering (ANSI escape codes)
└── utf8.c           - UTF-8 character handling
```

### Header Files

```
include/
├── common.h         - Common definitions, constants
├── ssh_server.h     - SSH server interface
├── chat_room.h      - Chat room interface
├── message.h        - Message structure and persistence
├── tui.h            - TUI rendering functions
└── utf8.h           - UTF-8 utilities
```

### Key Data Structures

#### `client_t` (ssh_server.h)
```c
typedef struct client {
    ssh_session session;
    ssh_channel channel;
    char username[MAX_USERNAME_LEN];
    int width, height;              // Terminal dimensions
    client_mode_t mode;             // INSERT/NORMAL/COMMAND
    int scroll_pos;
    bool connected;
    int ref_count;                  // Reference counting
    pthread_mutex_t ref_lock;
} client_t;
```

#### `chat_room_t` (chat_room.h)
```c
typedef struct {
    pthread_rwlock_t lock;          // Reader-writer lock
    struct client **clients;         // Dynamic array
    int client_count;
    message_t *messages;            // Ring buffer
    int message_count;
} chat_room_t;
```

#### `message_t` (message.h)
```c
typedef struct {
    time_t timestamp;
    char username[MAX_USERNAME_LEN];
    char content[MAX_MESSAGE_LEN];
} message_t;
```

---

## Building and Testing

### Build Targets

```sh
make              # Standard release build
make debug        # Debug build with symbols (-g)
make asan         # AddressSanitizer build
make check        # Static analysis (cppcheck)
make clean        # Clean build artifacts
make install      # Install to /usr/local/bin
```

### Compiler Flags

**Release build:**
```
-Wall -Wextra -O2 -std=c11 -D_XOPEN_SOURCE=700
```

**Debug build:**
```
-Wall -Wextra -g -O0 -std=c11 -D_XOPEN_SOURCE=700
```

**ASAN build:**
```
-Wall -Wextra -g -O0 -fsanitize=address -fno-omit-frame-pointer
```

### Running Tests

```sh
make test         # Run all tests

# Individual tests
cd tests
./test_basic.sh              # Basic functionality
./test_security_features.sh  # Security checks
./test_anonymous_access.sh   # Anonymous access
./test_stress.sh             # Concurrent connections
```

### Test Coverage

- **Basic**: Server startup, SSH connection, message logging
- **Security**: RSA keys, env vars, UTF-8 validation, buffer overflow protection
- **Anonymous**: Passwordless access, any username
- **Stress**: 10 concurrent clients for 30 seconds

---

## Core Components

### 1. SSH Server (ssh_server.c)

**Callback-based API** (libssh 0.9+):

```c
/* Authentication callbacks */
static int auth_password(ssh_session session, const char *user,
                         const char *password, void *userdata);
static int auth_none(ssh_session session, const char *user, void *userdata);

/* Channel callbacks */
static ssh_channel channel_open_request_session(ssh_session session, void *userdata);
static int channel_pty_request(ssh_session session, ssh_channel channel,
                               const char *term, int width, int height,
                               int pxwidth, int pxheight, void *userdata);
static int channel_shell_request(ssh_session session, ssh_channel channel,
                                 void *userdata);
static int channel_exec_request(ssh_session session, ssh_channel channel,
                                const char *command, void *userdata);
```

**Event loop:**
```c
ssh_event event = ssh_event_new();
ssh_event_add_session(event, session);

/* Wait for: auth_success, channel != NULL, channel_ready */
while ((!ctx->auth_success || ctx->channel == NULL || !ctx->channel_ready) && !timed_out) {
    ssh_event_dopoll(event, 1000);
}
```

### 2. Chat Room (chat_room.c)

**Thread-safe broadcasting:**
```c
void room_broadcast(chat_room_t *room, const message_t *msg) {
    pthread_rwlock_wrlock(&room->lock);

    /* Copy client list with ref counting */
    client_t **clients_copy = calloc(...);
    for (int i = 0; i < count; i++) {
        clients_copy[i]->ref_count++;
    }

    pthread_rwlock_unlock(&room->lock);  // Release lock early

    /* Render outside lock (avoid deadlock) */
    for (int i = 0; i < count; i++) {
        tui_render_screen(clients_copy[i]);
        client_release(clients_copy[i]);
    }
}
```

**Why this works:**
- Copy client list while holding write lock
- Increment reference counts
- Release lock BEFORE rendering
- Render to all clients outside lock
- Decrement reference counts (may free clients)

### 3. Message Persistence (message.c)

**Log format:**
```
2024-01-13T10:30:45Z|username|message content
```

**Optimized loading** (backward scan):
```c
/* Scan backwards from file end */
fseek(fp, 0, SEEK_END);
long file_size = ftell(fp);
long pos = file_size - 1;

/* Read 4KB chunks backwards */
#define CHUNK_SIZE 4096
while (pos >= 0 && newlines_found < max_messages) {
    /* Read chunk */
    /* Count newlines backwards */
    /* Stop when max_messages found */
}
```

**Complexity:** O(last N messages) instead of O(file size)

### 4. TUI Rendering (tui.c)

**Flicker-free rendering:**
```c
/* Move to top (no clear screen!) */
pos += snprintf(buffer + pos, size - pos, ANSI_HOME);

/* Render each line with line clear */
for (each line) {
    pos += snprintf(buffer + pos, size - pos, "%s\033[K\r\n", line);
}
```

**ANSI codes:**
- `\033[H` - HOME (move cursor to 0,0)
- `\033[K` - EL (erase to end of line)
- `\033[2J` - ED (erase display) - **DON'T USE** (causes flicker)

### 5. UTF-8 Handling (utf8.c)

**Character width:**
```c
int utf8_char_width(const char *str) {
    unsigned char c = (unsigned char)str[0];

    /* ASCII */
    if (c < 0x80) return 1;

    /* CJK ranges (width = 2) */
    uint32_t codepoint = /* decode UTF-8 */;
    if (codepoint >= 0x4E00 && codepoint <= 0x9FFF) return 2;  // CJK Unified
    if (codepoint >= 0x3040 && codepoint <= 0x30FF) return 2;  // Hiragana/Katakana
    if (codepoint >= 0xAC00 && codepoint <= 0xD7AF) return 2;  // Hangul

    return 1;
}
```

**Word deletion (Ctrl+W):**
```c
void utf8_remove_last_word(char *str) {
    int i = strlen(str);

    /* Skip trailing spaces */
    while (i > 0 && str[i-1] == ' ') i--;

    /* Skip non-spaces (the word) */
    while (i > 0 && str[i-1] != ' ') i--;

    str[i] = '\0';
}
```

**Note:** Only recognizes ASCII space as word boundary (not ideal for CJK).

---

## Adding Features

### Adding a New Command

1. **Add to `execute_command()` in ssh_server.c:**
```c
if (strcmp(cmd, "newcmd") == 0) {
    pos += snprintf(output + pos, sizeof(output) - pos,
                   "New command output\n");
}
```

2. **Update help text in tui.c:**
```c
"AVAILABLE COMMANDS:\n"
"  newcmd         - Description of new command\n"
```

3. **Add test in tests/test_basic.sh:**
```sh
echo ":newcmd" | timeout 5 ssh -p $PORT localhost
```

### Adding a New Keybinding

1. **Add to `handle_key()` in ssh_server.c:**
```c
case MODE_INSERT:
    if (key == 26) {  /* Ctrl+Z */
        /* Handle Ctrl+Z */
        return true;
    }
```

2. **Update help text in tui.c**

3. **Document in README.md**

---

## Debugging

### Enable Verbose SSH Logging

```sh
TNT_SSH_LOG_LEVEL=4 ./tnt
```

Levels:
- 0 = No logs
- 1 = Warnings (default)
- 2 = Protocol
- 3 = Packet
- 4 = Functions

### Use AddressSanitizer

```sh
make asan
ASAN_OPTIONS=detect_leaks=1 ./tnt
```

Detects:
- Buffer overflows
- Use-after-free
- Memory leaks
- Stack corruption

### Use Valgrind

```sh
make
valgrind --leak-check=full --show-leak-kinds=all ./tnt
```

### GDB Debugging

```sh
make debug
gdb ./tnt

(gdb) run
(gdb) bt        # backtrace on crash
(gdb) p var     # print variable
```

### Common Issues

**Problem:** Client disconnects immediately
**Solution:** Check `handle_pty_request()` - must wait for shell/exec request

**Problem:** Rendering flickers
**Solution:** Use `\033[K` (line clear) instead of `\033[2J` (screen clear)

**Problem:** Race condition / deadlock
**Solution:** Check lock order, use reader-writer locks correctly

**Problem:** Memory leak
**Solution:** Run with ASAN, check reference counting

---

## Performance Optimization

### Hot Paths

1. **Message broadcasting** (`room_broadcast`)
   - Minimize lock holding time
   - Render outside lock
   - Use fixed-size buffers

2. **TUI rendering** (`tui_render_screen`)
   - Build buffer completely, then single write
   - No incremental writes
   - Use `snprintf` (not `strcat`)

3. **Message loading** (`message_load`)
   - Backward scan from file end
   - 4KB chunked reads
   - Early termination

### Profiling

```sh
# Compile with profiling
make CFLAGS="-pg"

# Run
./tnt

# Generate report
gprof tnt gmon.out > profile.txt
```

### Benchmarking

```sh
# Message throughput
time (for i in {1..1000}; do echo "msg $i"; done | ssh -p 2222 localhost)

# Concurrent connections
./tests/test_stress.sh

# Startup time with large log
dd if=/dev/zero of=messages.log bs=1M count=10
time ./tnt
```

---

## Contributing Guidelines

### Code Style

Follow **Linux kernel coding style**:

```c
/* Function brace on new line */
int function(int arg)
{
    /* Use tabs (width 8) */
    if (condition) {
        /* Do something */
    }

    /* Single exit point preferred */
    return ret;
}
```

**Rules:**
- Tabs for indentation (8 spaces)
- Max 80 columns
- No trailing whitespace
- `/* C-style comments */` (not `//`)
- Functions < 100 lines
- Max 3 levels of indentation

### Commit Messages

```
subsystem: short description (max 50 chars)

Longer explanation if needed (wrap at 72 chars).
Explain WHAT and WHY, not HOW.

- Bullet points OK
- Reference issues: Fixes #123

Signed-off-by: Your Name <email@example.com>
```

Examples:
```
ssh: migrate to callback-based API

Replace deprecated message-based API with modern callback-based
server implementation. Eliminates message loop complexity.

tui: optimize rendering to eliminate flicker

Use ANSI HOME + line clear instead of full screen clear.
Reduces rendering time from 50ms to <1ms.
```

### Pull Request Process

1. **Fork and branch**
   ```sh
   git checkout -b feature/amazing-feature
   ```

2. **Make changes**
   - Write code
   - Add tests
   - Update docs

3. **Test**
   ```sh
   make clean
   make
   make test
   make asan  # must pass!
   ```

4. **Commit**
   ```sh
   git commit -s -m "subsystem: description"
   ```

5. **Push and PR**
   ```sh
   git push origin feature/amazing-feature
   # Open PR on GitHub
   ```

### Code Review Checklist

- [ ] Follows Linux kernel code style
- [ ] All tests pass (`make test`)
- [ ] ASAN clean (`make asan`)
- [ ] No compiler warnings
- [ ] Documentation updated
- [ ] Commit messages follow convention
- [ ] No unnecessary complexity
- [ ] Thread-safe if touching shared data

---

## Additional Resources

- [libssh Documentation](https://api.libssh.org/stable/)
- [Linux Kernel Coding Style](https://www.kernel.org/doc/html/latest/process/coding-style.html)
- [ANSI Escape Codes](https://en.wikipedia.org/wiki/ANSI_escape_code)
- [UTF-8 Specification](https://en.wikipedia.org/wiki/UTF-8)

---

**Questions?** Open an issue or discussion on GitHub.
