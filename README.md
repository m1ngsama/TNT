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
./test_basic.sh         # functional
./test_stress.sh 50     # 50 clients
```

## Docs

- `README` - man page style
- `HACKING` - dev guide
- `DEPLOYMENT.md` - production
- `CICD.md` - automation
- `QUICKREF` - cheat sheet

## License

MIT
