# TNT

SSH-native terminal chat. One shared room, a Vim-style modal interface, and
anonymous access by default. The server is a single C daemon that speaks SSH
directly, so any `ssh` client is a full client.

Reference documentation ships as manual pages and is installed with the
binaries:

| Page | Covers |
| --- | --- |
| `tnt(8)` | server, options, environment, state directory |
| `tntctl(1)` | control client |
| `tnt-chat(7)` | interactive terminal interface and keybindings |
| `tnt-exec(7)` | non-interactive exec commands |
| `tnt-message-log(5)` | message log format and recovery |
| `tnt-module-protocol(7)` | `tnt.module.v1` module protocol |

## Install

Release installer, pinned to a tag (verifies `checksums.txt` before
installing):

```sh
curl -sSL https://raw.githubusercontent.com/m1ngsama/TNT/vX.Y.Z/install.sh | VERSION=vX.Y.Z sh
```

Replace `vX.Y.Z` with a tag from
[Releases](https://github.com/m1ngsama/TNT/releases). Omit `VERSION` to track
the latest release.

From source (needs a C compiler and the libssh development package):

```sh
git clone https://github.com/m1ngsama/TNT.git
cd TNT
make
sudo make install
```

Homebrew, Arch, and Debian packaging live under `packaging/`.

## Run

```sh
tnt                          # listens on 2222
tnt -p 3333 -d /var/lib/tnt
```

`tnt` stays in the foreground and does not fork; `tnt.service` is the systemd
unit (`sudo make install-systemd`).

## Connect

```sh
ssh -p 2222 chat.example.com
```

Any username and any password are accepted, including an empty one. The room
is anonymous and unrestricted by design.

## Operate

```sh
tntctl -p 2222 localhost health
tntctl -p 2222 localhost stats
tntctl -p 2222 localhost tail 20
```

`tntctl` accepts only the commands defined by `tnt-exec(7)` and launches `ssh`
without a shell. Every exec command also works over a plain
`ssh -p 2222 host <command>`.

## Modules

Modules are external processes that exchange JSON Lines with the server over
stdio. They stay disabled unless `TNT_MODULE_PATHS` names their directories,
and the core must keep working with all of them off. See
`tnt-module-protocol(7)`, validate a module with `scripts/module_check.sh`, and
find community modules in
[m1ngsama/tnt-modules](https://github.com/m1ngsama/tnt-modules).

## Develop

```sh
make            # build tnt and tntctl
make test       # unit, script, and integration suites
make check      # cppcheck and clang-tidy
make asan       # AddressSanitizer build
```

`scripts/release_check.sh` runs the release preflight. `MAINTAINERS` maps every
path to an area; `scripts/get_maintainer.sh <path>` resolves one.

## License

MIT. See `LICENSE`.
