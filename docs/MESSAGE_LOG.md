# Message Log

This document defines the persisted chat-history format used by TNT 1.x.

## Format: `messages.log` v1

Each record is one UTF-8 line:

```text
RFC3339_UTC|username|content\n
```

Example:

```text
2026-05-27T12:34:56Z|alice|hello
```

Rules:

- Timestamp is strict UTC RFC3339: `YYYY-MM-DDTHH:MM:SSZ`.
- The separator is literal `|`.
- A valid record has exactly three fields and exactly two separators.
- `username` and `content` must be non-empty valid UTF-8.
- `username` must fit `MAX_USERNAME_LEN`; `content` must fit
  `MAX_MESSAGE_LEN`.
- Every complete record ends with `\n`.

The file has no header.  The version is defined by this record contract so
existing append-only logs remain readable.

## Write Behavior

`message_save()` sanitizes fields before appending:

- `|`, `\n`, and `\r` in usernames become `_`.
- `|`, `\n`, and `\r` in content become spaces.
- Timestamps are written in UTC.

Private messages are not written to `messages.log`.

## Replay And Search

Replay and search use the same strict parser.  TNT skips records that are:

- malformed or missing fields
- invalid UTF-8
- too long
- outside the accepted timestamp window
- terminated without a trailing newline
- written with extra separators

Skipping a bad record is intentional recovery behavior.  A truncated final
line is treated as a partial append and ignored rather than replayed.

## Compatibility

The v1 record format is stable for TNT 1.x.  Future incompatible storage
changes must document downgrade behavior in release notes and provide an
operator-visible migration or export path.
