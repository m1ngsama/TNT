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
The installer verifies downloaded release binaries against `checksums.txt`
before installing them. Older releases may provide only `tnt`; newer releases
also install `tntctl`.

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
tnt -d /var/lib/tnt
PORT=3333 tnt    # via env var
```

### Connecting

```sh
ssh -p 2222 chat.example.com
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
Paste      - Multi-line paste stays in the input buffer
```

The input line shows remaining bytes near the message limit.  Extra input
past the limit is ignored with a terminal bell.

**NORMAL mode**
```
Opens at latest messages
Stays pinned to latest until you scroll up
i          - Return to INSERT mode
:          - Enter COMMAND mode
j/k        - Scroll down/up one line
Ctrl+D/U   - Scroll half page down/up
Ctrl+F/B   - Scroll full page down/up
PgDn/PgUp  - Scroll full page down/up
End/Home   - Jump to bottom/top
g/G        - Jump to top/bottom
?          - Show full key reference
Ctrl+C     - Exit chat
```

**COMMAND mode**
```
:list, :users        - Show online users
:nick <name>         - Change nickname
:msg <user> <message> - Send private message
:w <user> <text>     - Short alias for :msg
:inbox               - Show private messages
:last [N]            - Show last N messages from history (max 50, default 10)
:search <keyword>    - Search full message history (case-insensitive)
:mute-joins          - Toggle join/leave system notifications
:lang <en|zh>        - Switch UI language for this session
:help                - Show concise manual
:clear               - Clear command output
:q, :quit, :exit     - Disconnect
Up/Down              - Browse command history
ESC                  - Return to NORMAL mode
```

Command output pages use `j/k`, `Ctrl+D/U`, and `g/G` for paging.  `:inbox`
is live: press `r` to refresh it manually, and it refreshes when a new private
message arrives while the inbox is open.

**Special messages (INSERT mode)**
```
/me <action>         - Send action (e.g. /me waves)
@username            - Mention user (bell + highlight)
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

# Store host key and logs in an explicit state directory
TNT_STATE_DIR=/var/lib/tnt tnt

# Show the public SSH endpoint in startup logs
TNT_PUBLIC_HOST=chat.example.com tnt

# Choose interactive UI language (en or zh; defaults from locale)
TNT_LANG=zh tnt
```

The same operational settings can be passed explicitly, which is often
clearer in package scripts and one-off test deployments:

```sh
tnt \
  --bind 127.0.0.1 \
  --public-host chat.example.com \
  --max-connections 100 \
  --max-conn-per-ip 10 \
  --max-conn-rate-per-ip 30 \
  --idle-timeout 3600 \
  -p 2222 \
  -d /var/lib/tnt
```

**Rate limiting:**
```sh
# Max total connections (default 64)
TNT_MAX_CONNECTIONS=100 tnt

# Max concurrent sessions per IP (default 5)
TNT_MAX_CONN_PER_IP=10 tnt

# Max new connection attempts per IP in 60 seconds (default 10)
TNT_MAX_CONN_RATE_PER_IP=30 tnt

# Disable connection-rate and auth-failure blocking (testing only)
TNT_RATE_LIMIT=0 tnt

# Idle timeout in seconds (default 1800 = 30min, 0 to disable)
TNT_IDLE_TIMEOUT=3600 tnt
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
TNT_MAX_CONN_PER_IP=30 \
TNT_MAX_CONN_RATE_PER_IP=60 \
TNT_SSH_LOG_LEVEL=1 \
tnt -p 2222
```

### SSH Exec Interface

TNT also exposes a small non-interactive SSH surface for scripts:

```sh
ssh -p 2222 chat.example.com health
ssh -p 2222 chat.example.com stats --json
ssh -p 2222 chat.example.com users
ssh -p 2222 chat.example.com "tail -n 20"
ssh -p 2222 chat.example.com "dump -n 100"
ssh -p 2222 operator@chat.example.com post "service notice"
ssh -p 2222 chat.example.com post "/me deploys v2.0"
```

**`post` identity**: the message is attributed to the SSH login name (the `user@` part of the URL, falling back to `anonymous`). In the default anonymous-access configuration there is no identity check, so any client can post as any name. Set `TNT_ACCESS_TOKEN` if you need authenticated posting.

See [docs/INTERFACE.md](docs/INTERFACE.md) for the stable exec command
contract, exit statuses, and JSON field definitions.

Source and package-manager installs also include `tntctl`, a thin wrapper
around the same SSH exec interface:

```sh
tntctl chat.example.com health
tntctl -p 2222 chat.example.com stats --json
tntctl -p 2222 chat.example.com dump -n 100
tntctl -l operator chat.example.com post "service notice"
```

### Log Maintenance

Persisted public history is stored as `messages.log` in the TNT state
directory.  For manual maintenance, archive and compact it with:

```sh
scripts/logrotate.sh /var/lib/tnt/messages.log 100 10000
```

The script archives the full log, keeps the last `KEEP_LINES` records in the
active file, compresses the archive when `gzip` is available, and can be
previewed with `--dry-run`.

## Development

### Building

```sh
make              # standard build
make debug        # debug build (with symbols)
make asan         # AddressSanitizer build
make release-check # local release/package preflight
make check        # static analysis (cppcheck)
make clean        # clean build artifacts
```

### Testing

```sh
make test          # run comprehensive test suite and fail on regressions
make test-advisory # run integration tests as advisory checks
make anonymous-access-test # verify default anonymous login behavior
make connection-limit-test # verify per-IP concurrency and rate limits
make security-test # run security feature checks
make stress-test   # run configurable concurrent-client stress test
make soak-test     # run idle/reconnect/control-plane soak test
make slow-client-test # run slow interactive-client backpressure test
make user-lifecycle-test # run a two-user TUI lifecycle test
make ci-test       # run the same checks as GitHub Actions

# Individual tests
cd tests
./test_basic.sh              # basic functionality
./test_security_features.sh  # security features
./test_anonymous_access.sh   # anonymous access
./test_connection_limits.sh  # per-IP concurrency and rate limits
./test_stress.sh             # stress test
./test_soak.sh               # soak test
./test_slow_client.sh        # slow-client backpressure
./test_user_lifecycle.sh     # two-user TUI lifecycle
```

**Test coverage:**
- Basic functionality: 3 tests
- Anonymous access: 2 tests
- Security features: 12 tests
- Stress test: configurable concurrent clients (`CLIENTS=20 DURATION=60 make stress-test`)
- Slow-client test: an unread interactive SSH client cannot block health,
  stats, post, tail, or server survival checks

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
│   ├── cli_text.c    # startup CLI help and option text
│   ├── command_catalog.c # command metadata, usage, and argument shape
│   ├── commands.c    # COMMAND-mode command dispatch
│   ├── exec_catalog.c # SSH exec command matching, usage, and argument shape
│   ├── exec.c        # SSH exec command dispatch
│   ├── tntctl.c      # local wrapper around the SSH exec interface
│   ├── ssh_server.c  # SSH server implementation
│   ├── bootstrap.c   # SSH authentication and session bootstrap
│   ├── chat_room.c   # chat room logic
│   ├── message.c     # message persistence
│   ├── history_view.c # message viewport and scroll state
│   ├── help_text.c   # full-screen key reference content
│   ├── manual.c      # concise manual panel rendering
│   ├── manual_text.c # concise manual content
│   ├── i18n.c        # UI language and locale selection
│   ├── i18n_text.c   # shared UI text catalog
│   ├── ratelimit.c   # connection limits and rate limiting
│   ├── tui.c         # terminal UI rendering
│   ├── tui_status.c  # status/input line rendering
│   └── utf8.c        # UTF-8 character handling
├── include/          # header files
├── tests/            # test scripts
├── docs/             # documentation
├── packaging/        # package-manager drafts and release checklist
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

# Optional: override defaults without editing the unit
sudo tee /etc/default/tnt >/dev/null <<'EOF'
PORT=2222
TNT_BIND_ADDR=0.0.0.0
TNT_STATE_DIR=/var/lib/tnt
TNT_MAX_CONNECTIONS=200
TNT_MAX_CONN_PER_IP=30
TNT_MAX_CONN_RATE_PER_IP=60
TNT_RATE_LIMIT=1
TNT_SSH_LOG_LEVEL=0
TNT_PUBLIC_HOST=chat.example.com
EOF
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

## Packaging

Package-manager drafts live in [packaging/](packaging/). Current targets are
Arch/AUR (`tnt-chat`), Homebrew tap formula, and Ubuntu PPA notes.

Before preparing a release locally:

```sh
make release-check
```

Longer local preflight can opt into runtime soak and slow-client coverage:

```sh
RUN_SOAK=1 RUN_SLOW_CLIENT=1 make release-check
```

Before publishing package recipes, replace placeholder checksums and run:

```sh
make release-check-strict
```

## Files

```
messages.log    - Chat history (RFC3339 format)
host_key        - SSH host key (auto-generated, 4096-bit RSA)
motd.txt        - Message of the Day (optional, shown to users on connect)
tnt.service     - systemd service unit
```

The persisted chat-history format is documented in
[docs/MESSAGE_LOG.md](docs/MESSAGE_LOG.md).

### MOTD (Message of the Day)

Place a `motd.txt` file in the state directory to show a welcome message to every user on connect. Users see the MOTD before entering the chat and press any key to continue.

```sh
# Example (assuming default state dir)
cat > motd.txt <<'EOF'
Welcome to the chat server!
Be respectful. No spam.
EOF
```

Delete `motd.txt` to disable the MOTD.

## Documentation

- [Development Guide](https://github.com/m1ngsama/TNT/wiki/Development-Guide) - Complete development manual
- [Quick Setup](docs/EASY_SETUP.md) - 5-minute deployment guide
- [Roadmap](docs/ROADMAP.md) - Long-term Unix/GNU direction and next stages
- [Interface Contract](docs/INTERFACE.md) - Scriptable commands, exit statuses, and JSON fields
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

## Troubleshooting

### "Connection closed by remote host" right after `ssh -p 2222 host`

TNT has very little it can say to the SSH client before disconnecting,
so any pre-auth rejection just looks like a generic close.  Common
causes, fastest to slowest fix:

| Likely cause | Why | Fix |
|---|---|---|
| Per-IP concurrent limit | `TNT_MAX_CONN_PER_IP` (default 5) | Close other sessions, or raise the env var |
| Per-IP connection rate | More than `TNT_MAX_CONN_RATE_PER_IP` attempts in 60 s | Wait 5 min (block window), or raise the limit |
| Auth-failure ban | 5 wrong passwords / failed kex in a row | Wait 5 min |
| Global cap | `TNT_MAX_CONNECTIONS` (default 64) is full | Wait for someone to leave |
| Firewall | The host's ufw / iptables doesn't open 2222 | Open the port |

The server admin can confirm which by checking the systemd journal
(`sudo journalctl -u tnt -n 50 --no-pager`) — the rejection reason is
logged to stderr with the offending IP.

### Idle disconnect

After `TNT_IDLE_TIMEOUT` seconds (default 1800 = 30 min) of no
keystrokes, TNT prints a localized idle-timeout notice and closes the
channel. Set the env var to `0` to disable.

## Known Limitations

- Single chat room (no multi-room support yet)
- TUI displays at most 100 messages at once; use `:last N` or `:search` to access older history from disk
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
