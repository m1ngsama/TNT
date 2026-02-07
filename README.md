# TNT

Terminal chat server. Vim-style interface. SSH-based.

## Install

```sh
curl -sSL https://raw.githubusercontent.com/m1ngsama/TNT/main/install.sh | sh
```

Or download from [releases](https://github.com/m1ngsama/TNT/releases).

## Run

```sh
tnt              # port 2222
tnt -p 3333      # custom port
PORT=3333 tnt    # env var
```

Connect: `ssh -p 2222 localhost`

**Anonymous Access**: By default, users can connect with ANY username and ANY password (or empty password). No SSH keys required. This makes TNT perfect for public chat servers.

## Security

Configure via environment variables.

### Access Control

```sh
TNT_ACCESS_TOKEN="secret" tnt           # require password
TNT_BIND_ADDR=127.0.0.1 tnt             # localhost only
```

Without `TNT_ACCESS_TOKEN`, server is open (default).

### Rate Limiting

```sh
TNT_MAX_CONNECTIONS=100 tnt             # total limit
TNT_MAX_CONN_PER_IP=10 tnt              # per-IP limit
TNT_RATE_LIMIT=0 tnt                    # disable (testing only)
```

Default: 64 total, 5 per IP, rate limiting enabled.

### SSH Options

```sh
TNT_SSH_LOG_LEVEL=3 tnt                 # verbose logging (0-4)
```

## Keys

**INSERT** (default)
- `ESC` → NORMAL
- `Enter` → send
- `Backspace` → delete

**NORMAL**
- `i` → INSERT
- `:` → COMMAND
- `j/k` → scroll
- `g/G` → top/bottom
- `?` → help

**COMMAND**
- `:list` → users
- `:help` → commands
- `ESC` → back

## Build

```sh
make              # normal
make debug        # with symbols
make asan         # sanitizer
make check        # static analysis
```

Requires: `libssh`

## Deploy

See [DEPLOYMENT.md](DEPLOYMENT.md) for systemd setup.

## Files

```
messages.log      chat history
host_key          SSH key (auto-gen)
tnt.service       systemd unit
```

## Test

```sh
make test               # run comprehensive test suite
# Individual tests are in tests/ directory
```

## Docs

- `README` - man page style
- `EASY_SETUP.md` - 🚀 **快速部署指南 / Quick Setup Guide**
- `HACKING` - dev guide
- `DEPLOYMENT.md` - production
- `CICD.md` - automation
- `QUICKREF` - cheat sheet

## License

MIT
