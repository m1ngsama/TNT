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
- 📦 **Single binary** - Lightweight executable
- 🔒 **SSH access** - Secure encrypted connections
- 🖥️ **Auto terminal detection** - Adapts to your terminal size
- 💾 **Message persistence** - All messages saved to log file
- ⚡ **Low resource usage** - Minimal memory and CPU

## Building

**Dependencies:**
- libssh (required for SSH support)

**Install dependencies:**
```bash
# On macOS
brew install libssh

# On Ubuntu/Debian
sudo apt-get install libssh-dev

# On Fedora/RHEL
sudo dnf install libssh-devel
```

**Build:**
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
ssh -p 2222 localhost
```

The server will prompt for a password (any password is accepted) and then ask for your username.

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

- **Network**: Multi-threaded SSH server using libssh
- **TUI**: ANSI escape sequences with automatic terminal size detection
- **Storage**: Append-only log file
- **Concurrency**: pthread + rwlock
- **Security**: Encrypted SSH connections with host key authentication

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
- SSH protocol with PTY support for terminal size detection
- Dynamic window resize handling

## Development

```bash
make debug        # Build with debug symbols
make asan         # Build with AddressSanitizer
make check        # Run static analysis
./test_basic.sh   # Run basic tests
./test_stress.sh  # Run stress test
```

See [HACKING](HACKING) for development details.

## Security

- **Encrypted connections**: All traffic is encrypted via SSH
- **Host key authentication**: RSA host key generated on first run
- **Password authentication**: Currently accepts any password (customize for production)
- **Host key persistence**: Stored in `host_key` file
- **No plaintext**: Unlike telnet, all data is encrypted in transit

## License

MIT License - see LICENSE file
