# User Lifecycle

TNT solves one narrow problem: create a keyboard-first chat room that anyone
with an SSH client can join without installing a custom client.

The product path should stay short:

1. Operator installs `tnt`, chooses a state directory, and starts the server.
2. User connects with `ssh -p 2222 host`.
3. User picks a display name or presses Enter for `anonymous`.
4. User lands in INSERT mode at the live tail and can type immediately.
5. User presses Esc to browse history with Vim-style movement.
6. User uses `:help` for the concise manual or `?` for the full key reference.
7. User searches from NORMAL with `/term`, or uses commands when needed:
   `:users`, `:msg`, `:reply`, `:inbox`, `:last`, `:search`, `:nick`,
   `:mute-joins`, and `:q`.
8. Scripts and operators use `tntctl` or SSH exec commands for `health`,
   `stats`, `users`, `tail`, `dump`, and `post`.

## TUI Experience Notes

- The first screen should make the product legible without reading external
  docs: this is an SSH chat room, not a shell.
- INSERT mode is the default because most users arrive to send a message.
- NORMAL mode opens at the latest messages, not the oldest history. Users can
  move upward for older context and use `G` or End to return to live chat.
- NORMAL mode accepts `/` as the fast path for history search, matching a
  common terminal-reader habit while reusing the existing `:search` command.
- INSERT mode keeps a small per-session sent-message history on Up/Down and
  completes trailing `@mention` prefixes with Tab.
- `:help` is a compact manual, while `?` is a full key reference. Do not add
  parallel support commands for the same task.
- Command syntax stays ASCII even in localized UI text. Translations explain;
  they do not change the command language.
- Private messages are visible in each participant's in-memory `:inbox`:
  recipients see incoming messages, senders see local sent-message copies,
  newest first.  They are not written to `messages.log` and do not survive a
  reconnect.
- `:inbox` is live enough for normal chat use: it can be refreshed with `r`
  and refreshes automatically when a new private message arrives while the
  inbox is open.
- `:reply` / `:r` keeps the private-message path keyboard-short: it answers
  the latest private-message peer in the current session without retyping a
  username.
- Long command output uses a small pager so `:last` and `:search` are readable
  on small terminals.

## Regression Coverage

`make user-lifecycle-test` runs a two-user SSH TUI journey:

- second user joins and is visible through `users --json`
- first user opens `?`, checks `:users`, sends a public message, scrolls, uses
  `:last` and `:search`
- first user toggles `:mute-joins`, sends two `:msg` messages, receives a
  `:reply`, confirms private-message copies in `:inbox`, changes nickname,
  sends `/me`, and exits
- second user opens `:inbox` before the private messages arrive, sees it
  auto-refresh after delivery, newest first, and replies without retyping the
  sender's username
- exec `tail` sees public messages
- `messages.log` contains public history and excludes private-message content

This test is intentionally closer to a user story than a unit regression. Keep
it focused on lifecycle guarantees, not every keybinding.
