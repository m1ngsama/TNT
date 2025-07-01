# TNT

**TNT's Not Tunnel** - A lightweight terminal chat server written in C

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Language](https://img.shields.io/badge/language-C-blue.svg)

## Features

- ✨ **Vim-style operations** - INSERT/NORMAL/COMMAND modes
- 📜 **Message history** - Browse with j/k keys
- 🕐 **Full timestamps** - Year-month-day hour:minute with timezone
- 📖 **Bilingual help** - Press ? for Chinese/English help
- 🌏 **UTF-8 support** - Full support for Chinese, Japanese, Korean
- 📦 **Single binary** - Lightweight ~50KB executable
- 🚀 **Telnet access** - No client installation needed
- 💾 **Message persistence** - All messages saved to log file
- ⚡ **Low resource usage** - Minimal memory and CPU

## Building

```bash
make
```

For debug build:
```bash
make debug
```

## Running

```bash
./tnt
```

Connect from another terminal:
```bash
telnet localhost 2222
```

## Usage

### Operating Modes

- **INSERT** - Type and send messages (default)
- **NORMAL** - Browse message history
- **COMMAND** - Execute commands

### Keyboard Shortcuts

#### INSERT Mode
- `ESC` - Enter NORMAL mode
- `Enter` - Send message
- `Backspace` - Delete character
- `Ctrl+C` - Exit

#### NORMAL Mode
- `i` - Return to INSERT mode
- `:` - Enter COMMAND mode
- `j` - Scroll down (older messages)
- `k` - Scroll up (newer messages)
- `g` - Jump to top
- `G` - Jump to bottom
- `?` - Show help
- `Ctrl+C` - Exit

#### COMMAND Mode
- `Enter` - Execute command
- `ESC` - Cancel, return to NORMAL
- `Backspace` - Delete character

### Available Commands

- `list`, `users`, `who` - Show online users
- `help`, `commands` - Show available commands
- `clear`, `cls` - Clear command output

## Architecture

- **Network**: Multi-threaded TCP server
- **TUI**: ANSI escape sequences
- **Storage**: Append-only log file
- **Concurrency**: pthread + rwlock

## Configuration

Set port via environment variable:

```bash
PORT=3333 ./tnt
```

## Technical Details

- Written in C11
- POSIX-compliant
- Thread-safe operations
- Proper UTF-8 handling for CJK characters
- Box-drawing characters for UI

## License

MIT License - see LICENSE file

## Development Timeline

- **July 2024**: Foundation & core functionality
- **August 2024**: TUI implementation & Vim modes
- **September 2024**: Polish, localization & v1.0 release
