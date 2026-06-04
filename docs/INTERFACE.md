# Interface Contract

This document defines the public surfaces that scripts, package tests, and
operators may rely on.

For 1.x, the public binary names are stable:

- `tnt` is the server process and daemon entrypoint.
- `tntctl` is a thin local wrapper around the SSH exec interface.

TNT will not introduce a separate `tntd` binary during 1.x.  If the project
ever splits the server into `tntd`, that change must ship with a major-version
compatibility plan, package migration notes, and a transition period for the
`tnt` command.

## Stability Scope

Stable:

- public binary names for 1.x: `tnt` and `tntctl`
- documented command-line flags in `tnt(1)`
- documented environment variables in `tnt(1)`
- SSH exec command names and argument shapes listed below
- SSH exec exit statuses
- JSON field names and value types for documented `--json` commands
- `messages.log` v1 record format documented in
  [MESSAGE_LOG.md](MESSAGE_LOG.md)

Not yet stable:

- exact human-readable diagnostic wording
- interactive TUI layout
- future storage migration tooling
- internal module names and helper functions

## Exit Status

TNT process startup and SSH exec commands use these exit statuses:

| Code | Name | Meaning |
|---:|---|---|
| 0 | `TNT_EXIT_OK` | Success |
| 1 | `TNT_EXIT_ERROR` | Runtime error, I/O error, allocation failure, persistence failure |
| 64 | `TNT_EXIT_USAGE` | Unknown command, invalid option, invalid argument shape |
| 69 | `TNT_EXIT_UNAVAILABLE` | Local `tntctl` SSH transport unavailable |
| 78 | `TNT_EXIT_CONFIG` | Reserved for future local `tntctl` configuration errors |

`64` follows the common `sysexits(3)` usage-error convention.

## SSH Exec Commands

Exec commands are run through a standard SSH client:

```sh
ssh -p 2222 chat.example.com health
ssh -p 2222 chat.example.com stats --json
ssh -p 2222 chat.example.com users --json
ssh -p 2222 chat.example.com "tail -n 20"
ssh -p 2222 chat.example.com "dump -n 100"
ssh -p 2222 operator@chat.example.com post "service notice"
```

The same commands can be run through `tntctl`:

```sh
tntctl chat.example.com health
tntctl -p 2222 chat.example.com stats --json
tntctl -p 2222 chat.example.com dump -n 100
tntctl -l operator chat.example.com post "service notice"
tntctl --host-key-checking accept-new chat.example.com users
```

### `health`

Prints:

```text
ok
```

Exit status: `0` when the daemon can accept and handle exec requests.

### `stats [--json]`

Text output is line-oriented key/value data:

```text
status ok
online_users 0
message_count 0
client_capacity 64
active_connections 1
uptime_seconds 12
```

JSON output:

```json
{
  "status": "ok",
  "online_users": 0,
  "message_count": 0,
  "client_capacity": 64,
  "active_connections": 1,
  "uptime_seconds": 12
}
```

Field names and scalar types are stable.  New fields may be added in a minor
release.

### `users [--json]`

Text output prints one username per line.

JSON output is an array of strings:

```json
["alice", "bob"]
```

### `tail [N]` / `tail -n N`

Prints recent in-memory messages as tab-separated lines:

```text
2026-05-25T12:00:00Z	alice	hello
```

The current upper bound is `MAX_MESSAGES`.  This command reads the live
in-memory room buffer, not the full persisted log.

### `dump [N]` / `dump -n N`

Exports valid persisted `messages.log` v1 records in chronological order:

```text
2026-05-25T12:00:00Z|alice|hello
```

Without `N`, `dump` exports all valid persisted records.  With `N`, it exports
the last `N` valid persisted records.  Malformed, invalid UTF-8, oversized, or
truncated records are skipped by the same strict parser used for replay and
search.

This command reads the on-disk log, not the live in-memory room buffer.  A
missing log produces empty output and exit status `0`.

### `post MESSAGE`

Posts a message as the SSH login name and prints:

```text
posted
```

In anonymous-access mode, the SSH login name is not authenticated.  Operators
should configure `TNT_ACCESS_TOKEN` before relying on exec-post identity.

## Interactive Private Messages

`:msg user message` and its `:w` alias deliver private messages only to online
interactive clients.  `:reply message` and its `:r` alias send to the latest
private-message peer in the current session.  Private messages are not
persisted to `messages.log` and are not included in exec `tail`, exec `dump`,
`:last`, or `:search`.

Each participant keeps a bounded in-memory `:inbox` for the current session.
Recipients see incoming private messages; senders see local sent-message
copies.  Unread incoming messages are marked with `*` until `:inbox` renders.
`:inbox` displays newest messages first, shows a transient unread count, can
be refreshed with `r`, and refreshes automatically while open when a new
private message arrives.
`:inbox clear` removes the current session's private messages, unread count,
and reply target.

### `help`

Prints a localized human-readable command summary.  It is intended for people,
not parsers.

## `tntctl`

`tntctl` preserves the command names, exit statuses, and JSON schemas above.
It invokes the local `ssh(1)` client without a local shell.  OpenSSH transport
failures are mapped to `TNT_EXIT_UNAVAILABLE` (`69`); remote TNT exec statuses
are otherwise returned unchanged.

The wrapper intentionally does not accept arbitrary SSH options or a password
option.  It exposes only bounded host-key options:
`--host-key-checking yes|accept-new|no` and `--known-hosts FILE`.  Use normal
SSH configuration for jump hosts, identity files, and authentication.  If the
server requires `TNT_ACCESS_TOKEN`, enter it through the normal SSH password
prompt or use an SSH setup appropriate for the deployment.
