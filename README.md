# TNT - Terminal Network Talk

A minimalist terminal chat server with Vim-style interface over SSH.

## Features

- **Zero config** - Download and run, auto-generates SSH keys
- **SSH-based** - Leverage mature SSH protocol for encryption and auth
- **Vim-style UI** - Modal editing (INSERT/NORMAL/COMMAND)
- **UTF-8 native** - Full Unicode support
- **High performance** - Pure C, multi-threaded, sub-100ms startup
- **Secure** - Rate limiting, auth failure protection, input validation
- **Persistent** - Auto-saves chat history
- **Elegant** - Flicker-free TUI rendering

## Quick Start

### Installation

**One-liner:**
```sh
curl -sSL https://raw.githubusercontent.com/m1ngsama/TNT/main/install.sh | sh
```

**From source:**
```sh
git clone https://github.com/m1ngsama/TNT.git
cd TNT
make
sudo make install
```

**Binary releases:**
https://github.com/m1ngsama/TNT/releases

### Running

```sh
tnt              # default port 2222
tnt -p 3333      # custom port
PORT=3333 tnt    # via env var
```

### Connecting

```sh
ssh -p 2222 localhost
```

**Anonymous access by default**: Users can connect with ANY username/password (or empty password). No SSH keys required. Perfect for public chat servers.

## Usage

### Keybindings

**INSERT mode (default)**
```
ESC        - Enter NORMAL mode
Enter      - Send message
Backspace  - Delete character
Ctrl+W     - Delete last word
Ctrl+U     - Delete line
Ctrl+C     - Enter NORMAL mode
```

**NORMAL mode**
```
i          - Return to INSERT mode
:          - Enter COMMAND mode
j/k        - Scroll down/up
g/G        - Scroll to top/bottom
?          - Show help
```

**COMMAND mode**
```
:list, :users, :who  - Show online users
:help, :commands     - Show available commands
:clear, :cls         - Clear command output
ESC                  - Return to NORMAL mode
```

### Security Configuration

**Access control:**
```sh
# Require password
TNT_ACCESS_TOKEN="secret" tnt

# Bind to localhost only
TNT_BIND_ADDR=127.0.0.1 tnt

# Bind to specific IP
TNT_BIND_ADDR=192.168.1.100 tnt
```

**Rate limiting:**
```sh
# Max total connections (default 64)
TNT_MAX_CONNECTIONS=100 tnt

# Max connections per IP (default 5)
TNT_MAX_CONN_PER_IP=10 tnt

# Disable rate limiting (testing only)
TNT_RATE_LIMIT=0 tnt
```

**SSH logging:**
```sh
# 0=none, 1=warning, 2=protocol, 3=packet, 4=functions (default 1)
TNT_SSH_LOG_LEVEL=3 tnt
```

**Production example:**
```sh
TNT_ACCESS_TOKEN="strong-password-123" \
TNT_BIND_ADDR=0.0.0.0 \
TNT_MAX_CONNECTIONS=200 \
TNT_MAX_CONN_PER_IP=3 \
TNT_SSH_LOG_LEVEL=1 \
tnt -p 2222
```

## Development

### Building

```sh
make              # standard build
make debug        # debug build (with symbols)
make asan         # AddressSanitizer build
make check        # static analysis (cppcheck)
make clean        # clean build artifacts
```

### Testing

```sh
make test         # run comprehensive test suite

# Individual tests
cd tests
./test_basic.sh              # basic functionality
./test_security_features.sh  # security features
./test_anonymous_access.sh   # anonymous access
./test_stress.sh             # stress test
```

**Test coverage:**
- Basic functionality: 3 tests
- Anonymous access: 2 tests
- Security features: 11 tests
- Stress test: concurrent connections

### Dependencies

- **libssh** (>= 0.9.0) - SSH protocol library
- **pthread** - POSIX threads
- **gcc/clang** - C11 compiler

**Ubuntu/Debian:**
```sh
sudo apt-get install libssh-dev
```

**macOS:**
```sh
brew install libssh
```

**Fedora/RHEL:**
```sh
sudo dnf install libssh-devel
```

## Project Structure

```
TNT/
├── src/              # source code
│   ├── main.c        # entry point
│   ├── ssh_server.c  # SSH server implementation
│   ├── chat_room.c   # chat room logic
│   ├── message.c     # message persistence
│   ├── tui.c         # terminal UI rendering
│   └── utf8.c        # UTF-8 character handling
├── include/          # header files
├── tests/            # test scripts
├── docs/             # documentation
├── scripts/          # operational scripts
├── Makefile          # build configuration
└── README.md         # this file
```

## Deployment

### systemd Service

```sh
sudo cp tnt.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable tnt
sudo systemctl start tnt
```

### Docker

```dockerfile
FROM alpine:latest
RUN apk add --no-cache libssh
COPY tnt /usr/local/bin/
EXPOSE 2222
CMD ["tnt"]
```

See [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md) for details.

## Files

```
messages.log    - Chat history (RFC3339 format)
host_key        - SSH host key (auto-generated, 4096-bit RSA)
tnt.service     - systemd service unit
```

## Documentation

- [Development Guide](https://github.com/m1ngsama/TNT/wiki/Development-Guide) - Complete development manual
- [Quick Setup](docs/EASY_SETUP.md) - 5-minute deployment guide
- [Security Reference](docs/SECURITY_QUICKREF.md) - Security config quick reference
- [Contributing](docs/CONTRIBUTING.md) - How to contribute
- [Changelog](docs/CHANGELOG.md) - Version history
- [CI/CD](docs/CICD.md) - Continuous integration setup
- [Quick Reference](docs/QUICKREF.md) - Command cheat sheet

## Performance

- **Startup**: < 100ms (even with 100k+ message history)
- **Memory**: ~2MB (idle)
- **Concurrency**: Supports 100+ concurrent connections
- **Throughput**: 1000+ messages/second

## Known Limitations

- Single chat room (no multi-room support yet)
- Keeps only last 100 messages in memory
- Ctrl+W only recognizes ASCII space as word boundary

## Contributing

Contributions welcome! See [CONTRIBUTING.md](docs/CONTRIBUTING.md)

**Process:**
1. Fork the repository
2. Create feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to branch (`git push origin feature/AmazingFeature`)
5. Open Pull Request

## License

MIT License - see [LICENSE](LICENSE)

## Acknowledgments

- [libssh](https://www.libssh.org/) - SSH protocol implementation
- Linux kernel community - Code style and engineering practices

## Contact

- Issues: https://github.com/m1ngsama/TNT/issues
- Pull Requests: https://github.com/m1ngsama/TNT/pulls

---

**"Talk is cheap. Show me the code."** - Linus Torvalds
