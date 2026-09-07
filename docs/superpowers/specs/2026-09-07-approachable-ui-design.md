# Approachable UI: non-modal default, optional vim mode, multi-line messages

Status: approved design, 2026-09-07
Target: TNT 1.4.0

## Goal

Make TNT usable by people with no terminal background, without taking anything
away from the people who use it today.

Three things change:

1. The default interface stops being modal. Typing always types.
2. Vim modal editing becomes an opt-in mode that keeps its current keys exactly.
3. Messages may contain newlines, and text with emoji, CJK, and long lines
   renders correctly.

## Evidence this is needed

Reproduced against 1.3.1 with a control group:

| Case | Result |
| --- | --- |
| type `helo world`, press Enter | message SENT |
| type `helo world`, press Left arrow, press Enter | **message LOST** |

`src/input.c` handles only `ESC [ A`, `ESC [ B`, and `ESC [ 2 0 0 ~` inside
INSERT mode. Left and Right arrows fall through to the plain-ESC branch at
`src/input.c:763`, which switches the session to NORMAL mode. The user's next
Enter is then a NORMAL-mode key and the composed text is discarded silently.

Two further defects, confirmed by reading the code:

- The input buffer is append-only (`src/input_buffer.c`). There is no cursor,
  so text can only be edited at its end.
- `utf8_char_width()` (`src/utf8.c:53`) covers CJK, Hangul, Kana, and fullwidth
  forms, but no emoji. `U+1F60C` returns width 1 while terminals render it in
  two columns, so every emoji in the room shifts alignment, truncation, and
  wrapping by one column. The production log contains such messages.

## Architecture

Three new pure modules, each with the repository's standard layout
(`include/x.h`, `src/x.c`, `tests/unit/test_x.c`, a MAINTAINERS entry, and a
`.gitignore` line for the test binary):

| Module | Owns | Does not own |
| --- | --- | --- |
| `editor` | multi-line buffer, cursor, UTF-8 and cluster boundaries, word and line operations, wrapping | key bytes, terminal output |
| `keymap` | byte sequence to intent, two tables (`default`, `vim`) | mutation of any state |
| `richtext` | markdown subset parsing, width-aware wrapping, output fragments | writing to the terminal |

Existing code changes shape rather than growing:

- `src/input.c` shrinks to a session loop plus intent dispatch.
- `src/tui.c` renders an input region of N rows instead of one.
- Newline escaping goes into the single existing parser in `src/message.c`.

### Data flow

```
SSH bytes -> input_buffer (UTF-8 state machine, existing)
          -> keymap lookup -> intent
          -> editor mutates state
          -> tui renders (N-row input region + richtext fragments)

send: editor text -> validation -> message log encode -> broadcast and append
```

### State ownership

Editor state moves into `struct client`, replacing the local `char input[]` in
`input_run_session()`. Rendering and command handling both need to read it.

The client struct must not grow. `insert_history[16][MAX_MESSAGE_LEN]` currently
costs 16 KiB per connection as a fixed array, and the deployment is gated at a
32 MiB cgroup baseline with `client_capacity` 50. The ring is therefore kept but
re-shaped: 8 entries held by heap pointer and sized to the message actually
sent, which frees more than the editor state costs. The acceptance condition is
measured, not assumed: per-connection resident cost must be no higher than
1.3.1 under the existing perf gate.

### Why an intent layer

The Left-arrow defect is not a missing branch, it is a missing abstraction: key
handling is spread across one large switch with an ESC fallthrough. Once keys
map to intents, an unrecognised escape sequence maps to `INTENT_NONE` and is
discarded. The class of bug becomes unrepresentable.

## Editor model

The buffer stays a single UTF-8 byte range capped at `MAX_MESSAGE_LEN`; multi
line messages share that budget. The cursor is a byte offset and the API
guarantees it always sits on a cluster boundary.

Two kinds of line must not be confused:

- **logical line** — separated by a real `\n` inside the message
- **display line** — a logical line wrapped to the terminal width

`Up`/`Down`, `Home`, and `End` operate on display lines, which is what the user
sees. Wrapping never splits a wide character and prefers breaking at spaces or
CJK boundaries.

Not built (YAGNI): undo/redo, full UAX #29 grapheme segmentation.

### Cluster rules

Width and cursor movement both operate on clusters, using four rules rather
than a full segmentation table:

1. base character plus combining marks (Mn/Me) — width of the base, marks are 0
2. base character plus a variation selector (`U+FE0F`, `U+FE0E`)
3. sequences joined by ZWJ (`U+200D`)
4. paired regional indicators (flags)

Emoji (Extended Pictographic) count as width 2. Combining marks and zero-width
joiners count as 0.

## Newline key

Terminals do not distinguish `Shift+Enter` from `Enter`; both send `\r` unless
the terminal implements the kitty keyboard protocol or `modifyOtherKeys`. Three
layers are therefore provided:

| Layer | Key | Availability |
| --- | --- | --- |
| preferred | `Shift+Enter` | terminals that answer the negotiated protocol |
| fallback | `Alt+Enter` (`ESC \r`) | most terminals |
| guaranteed | `Ctrl+J` (`\n`, distinct from `\r`) | every terminal |

TNT already negotiates bracketed paste (`\033[?2004h` at `src/input.c:1241`,
disabled at `:1627`); keyboard protocol negotiation follows the same
enable-on-entry, restore-on-exit pattern. The hint bar names the key that this
terminal actually supports rather than listing all three.

Multi-line **paste** depends on none of this. Bracketed paste is already
enabled; it is enough to stop replacing `\n` with a space.

## Default keymap

| Key | Behaviour |
| --- | --- |
| any printable character | inserts, never switches mode |
| `Enter` | send |
| `Shift+Enter` / `Alt+Enter` / `Ctrl+J` | newline |
| `Left` / `Right` | move the cursor |
| `Ctrl+Left` / `Ctrl+Right` | move by word |
| `Home` / `End` | start and end of the display line |
| `Up` / `Down` | recall the previous sent message when the input is empty; otherwise move between display lines |
| `PgUp` / `PgDn` | scroll the room history |
| `Backspace` / `Ctrl+W` / `Ctrl+U` | delete character, word, to line start (unchanged) |
| `Tab` | complete a nickname after `@`; complete a command when the line starts with `/` |
| `Esc` | dismiss a help or command panel; otherwise nothing, and never a mode change |
| `Ctrl+C` | clear the input; when already empty, point at `/quit` |

Commands use `/` in the default keymap (`/help`, `/users`, `/nick`), matching
what Discord, Slack, and Feishu users already know. `//` sends a literal `/`,
and only known commands are intercepted, so `/usr/local/bin` still sends as
text. The existing `/me` becomes an ordinary command instead of a special case
in INSERT mode.

## Vim mode

The current NORMAL, INSERT, and COMMAND behaviour is preserved key for key,
including `:` commands. Multi-line support is added consistently: `Enter` sends
and `Ctrl+J` inserts a newline in INSERT; `o` and `O` enter INSERT on a new
line, matching vim.

Enabling it, given that TNT is anonymous and has nowhere to persist a
preference:

1. a session command, available to anyone at any time
2. the SSH login name — `ssh vim@chat.example.com` — which today selects neither
   identity nor nickname and is therefore free to carry this signal; users put
   it in their `ssh_config` once
3. a server start flag, so an operator can make vim the default for a host

## Input region rendering

The input region grows from one row to at most six display rows. Beyond that it
scrolls internally and keeps the cursor visible. The history region shrinks
accordingly, and the existing `height < 4` clamp keeps small terminals safe.

## Message format

Records stay one per line with exactly two `|` separators. Inside the content
field, v2 records encode `\` as `\\` and a newline as `\n`.

### Why a migration is required

Old records may already contain a backslash — `C:\new`, or a user who literally
typed `\n`. Decoding every record with v2 rules would turn `C:\new` into
`C:<newline>ew`: silent corruption of existing history. Deployments through
Homebrew, AUR, and Debian have logs that cannot be inspected first, so this is
not a risk worth taking.

### Migration

On the first start after the upgrade:

1. detect that the log carries no v2 marker
2. back it up as `messages.log.v1.bak`
3. rewrite atomically, escaping `\` as `\\` in every old record (old records
   cannot contain a real newline, so that is the only transform)
4. write the marker; every record in the file is now v2 and decoding is
   unambiguous
5. remain idempotent — a second start must not escape twice

### Rollback

A 1.3.x binary reading a v2 log does not crash and does not drop records; it
displays `\n` and `\\` literally. That cosmetic degradation is the reason for
choosing backslash escaping over a version field, which would make old parsers
reject whole records as malformed.

### Byte limit

The 1023-byte content limit applies to the **encoded** form, so a newline costs
two bytes and the parser contract is unchanged. The input gauge counts encoded
length.

### Consumers

| Consumer | Change |
| --- | --- |
| startup replay | decode into memory |
| interactive search | search decoded text; matches may cross lines |
| `exec dump` / `tail` | **keep emitting the escaped form** — the one-record-per-line contract is what scripts depend on |
| `--log-check` | recognise v1 and v2, report an unmigrated log |
| `--log-recover` | repair both |

### Security boundary

Only `\n` is admitted. All other C0 and C1 control characters and DEL stay
rejected, for messages from clients and from modules alike. Without that line,
a user could send ANSI escape sequences to every terminal in the room. The
markdown subset is built entirely from ordinary printable characters and is
rendered server-side, which is what makes it safe.

### Module protocol

No format change. `src/json_text.c` already escapes `\n` when writing (line 27)
and unescapes it when parsing (line 176), so JSON Lines carries newlines today.
`tnt-module-protocol(7)` gains a note that `plain_text` may contain newlines.
`exec post` stays single-line.

## Rendering scope

| Layer | Contents |
| --- | --- |
| foundation | emoji, combining mark, and zero-width widths; wrap long lines in the history region instead of truncating; paste fidelity |
| minimal markdown | `` `inline code` ``, `**bold**`, fenced code blocks, URL highlighting |
| excluded | headings, lists, tables, nesting, italics |

Italics are excluded because `*x*` is ambiguous against `**x**` and because `*`
is ordinary punctuation in Chinese text.

## Testing

Unit tests, in the style of the existing `test_utf8` and `test_input_buffer`:

- `editor`: cursor invariants, cluster boundaries, wrapping, multi-line operations
- `keymap`: intent mapping for both tables, and that an unknown escape sequence
  maps to `INTENT_NONE`
- `richtext`: subset parsing, width, never splitting a wide character
- message log: encode/decode round-trip, v1 compatibility, migration idempotence

Integration tests, following the two established patterns in this repository
(`TNT_MAX_CONN_PER_IP=256 TNT_MAX_CONNECTIONS=256`, and a 15-iteration readiness
poll):

- **type, Left arrow, Enter must send** — the reproduced defect, as a permanent
  regression test
- multi-line paste fidelity, `Ctrl+J` newline, `/` commands and completion
- vim mode key-for-key regression, proving current behaviour is untouched
- migration: a v1 log starts, produces a backup with equivalent content, and a
  second start does not escape twice

Visual verification happens on the deployment host's live instance, which
currently has no real users.

Performance guardrails: the 32 MiB cgroup baseline and the existing perf gates
must still pass, and the client struct must not grow.

## Delivery order

The pieces have real dependencies, and the order below front-loads user-visible
repair so that each stage is shippable on its own:

1. **Width and wrapping foundation** — cluster rules in `utf8`, history-region
   wrapping. Independent of everything else, and fixes visible misalignment for
   every existing user.
2. **`editor` and `keymap` with both tables** — ends the Left-arrow data loss
   and gives cursor editing. The default and vim tables ship together, along
   with the three enable paths and the key-for-key vim regression suite: a
   release that switched the default away from modal editing without shipping
   the way back would strand today's users. Single-line only at this stage, so
   no format change is involved.
3. **Multi-line composition, message format v2, migration** — the irreversible
   step, taken alone so it can be verified and rolled back on its own.
4. **Markdown subset rendering.**
5. **Documentation and the release.**

Stages 1 and 2 together already resolve the reported input defects; stage 3 is
what the "multi-line experience is bad" complaint needs.

## Documentation

`tnt-chat(7)` is rewritten around the default keymap with vim mode as a section.
`tnt(8)` documents the server default flag. `tnt-message-log(5)` documents v2
encoding and migration. `tnt-module-protocol(7)` notes newlines in `plain_text`.
`README.md` gains nothing version-specific.
