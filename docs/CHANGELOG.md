# Changelog

## Unreleased

### Added
- Added a release tag/version guard used by the GitHub release workflow, so a
  `vX.Y.Z` tag must match `TNT_VERSION` before release assets are built.
- Added `make package-publish-check` for verifying Arch/Homebrew source
  checksums against the explicit release source archive after a tag exists.
- Added a release artifact gate that bundles Linux/macOS binaries, the explicit
  release source archive, and `checksums.txt` before opening the draft release.
- Added CI governance layers for fast PR checks, release-branch validation,
  extended runtime validation, container portability builds, and package recipe
  validation.
- Added a `config_defaults` module and unit coverage for runtime default
  values, env keys, and accepted numeric ranges.
- Added a dedicated `tntctl_text` module with unit coverage for local
  `tntctl` help and validation diagnostics.
- Documented the stable SSH exec interface contract, including exit statuses
  and JSON field shapes for package tests, scripts, and future `tntctl` work.
- Documented `messages.log` v1 as the stable TNT 1.x persisted history format,
  including parser, sanitization, and partial-record recovery rules.
- Added `dump [N]` / `dump -n N` to the SSH exec interface and `tntctl` for
  exporting valid persisted `messages.log` v1 records.
- Added regression-tested manual log archive and compaction coverage for
  `scripts/logrotate.sh`.
- Added offline `tnt --log-check` and `tnt --log-recover` modes for auditing
  and recovering valid `messages.log` v1 records without editing the source
  log in place.
- Added a public security policy, supported-version guidance, and GitHub issue
  templates for bug reports and feature requests.
- Added `tntctl`, a thin local wrapper around the documented SSH exec
  interface for health, stats, users, tail, post, help, and exit commands.
- Added explicit server configuration flags for bind address, public host,
  connection limits, rate limiting, idle timeout, and SSH log verbosity.
- Added a configurable soak test that keeps an interactive session open while
  repeatedly checking health, stats, users, reconnects, and post/tail behavior.
- Added a two-user TUI lifecycle regression test and user-lifecycle notes for
  the main onboarding, chat, help, history, search, private-message, nickname,
  action-message, and exit paths.
- Added a VHS tape draft for recording the core TNT terminal-chat experience.
- Added live `:inbox` refresh behavior: `r` refreshes the inbox manually, and
  an open inbox refreshes when a new private message arrives.
- Added `/` in NORMAL mode as a fast history-search entrypoint backed by the
  existing `:search` command.
- Added `make slow-client-test`, an opt-in regression for an unread
  interactive SSH client under backpressure while health, stats, post, tail,
  and server survival stay responsive.

### Changed
- INSERT-mode chrome now only advertises message sending and `Esc` to NORMAL;
  `? keys` appears only in NORMAL mode, matching where help keys work.
- Dismissing MOTD now returns first-time users to INSERT mode, and `Ctrl+C`
  closes the full key reference before it disconnects from NORMAL mode.
- COMMAND mode now accepts an optional leading `:` in typed commands, matching
  the way commands are written in the manual.
- `:search` output and docs now state that the command shows the last 15
  matches, avoiding the impression that the pager is a complete result set.
- Release checks now separate tag/source-archive readiness from package-manager
  checksum publishing, avoiding self-referential checksum requirements before
  the explicit release source archive exists.
- `tntctl --help` now gets its exec command list from `exec_catalog`, reducing
  duplicate command metadata between the local wrapper and SSH exec mode.
- Updated `tnt(1)` to document the current TUI search and pager keys, and
  added script coverage to keep active help surfaces free of removed support
  commands.
- `make install-systemd` now rewrites the installed unit's `ExecStart` to match
  the selected `PREFIX`/`BINDIR`, so package builds that install to `/usr`
  produce a unit pointing at `/usr/bin/tnt`.
- Release preflight now checks the staged systemd unit path, and strict release
  checks also require a clean tree, tag-at-HEAD, changelog release section, and
  non-placeholder maintainer metadata.
- CI and release workflows now use explicit least-privilege repository
  permissions.
- The release guide now documents SemVer expectations, manual release review,
  smoke testing, and rollback steps.
- Package installs now include `tntctl` and its man page alongside `tnt`.
- The binary naming policy is now explicit: `tnt` remains the stable 1.x
  server process name, and any future `tntd` split requires a major-version
  compatibility plan.
- SSH exec commands longer than the command buffer are now rejected with a
  usage error instead of being truncated and executed.
- SSH exec `post` now persists the message before broadcasting or returning
  `posted`, so persistence failures are not visible as successful room events.
- Mention and private-message bell notifications are now queued on the target
  client and flushed by that client's own session loop, so slow SSH writes do
  not block the sender's message path.
- Interactive client writes now pass through a bounded per-client outbox and
  flush against the remote SSH window from that client's session loop.  Exec
  sessions still write synchronously to preserve script output ordering.
- Session callback refs are now owned and released through `client.c`, so
  bootstrap and interactive cleanup no longer need to manually mirror the
  main-ref / callback-ref release sequence.
- Message-log replay and search now share one strict record parser and skip
  malformed, invalid UTF-8, extra-separator, oversized, or unterminated
  records instead of accepting partial replay data.
- `scripts/logrotate.sh` now has validated arguments, stable exit statuses,
  dry-run support, archive retention, gzip-aware archives, and a regression
  test in the normal test suite.
- `messages.log` v1 record parsing and formatting now live in a dedicated
  `message_log` module instead of being embedded in `message.c`.
- Offline message-log recovery shares the same `message_log` parser used by
  replay, search, and `dump`, so recovery behavior follows the documented v1
  contract.
- The two-user lifecycle test now covers opening `:inbox` before a private
  message arrives, matching the way users often leave an inbox page open.
- Help and command-output pagers now accept arrow keys, PgUp/PgDn, Home/End,
  and Space/`b` in addition to the existing Vim-style keys.
- Pre-login username entry now handles Ctrl+C/Ctrl+D cancel, Ctrl+U clear
  line, and Ctrl+W delete-word before the user joins the room.
- Long COMMAND-mode input is now left-truncated with a visible marker in the
  status line instead of wrapping and damaging the TUI.
- Private-message inbox access now uses its own mutex instead of sharing the
  SSH channel write lock, reducing unrelated contention on slow clients.
- Client writes now check the SSH channel's remote window before writing and
  mark the client disconnected when the window is closed, avoiding the most
  direct slow-reader blocking path.
- `make release-check` can now run the soak test with `RUN_SOAK=1`, keeping
  longer runtime checks opt-in for local release validation.
- `make release-check` can also run the slow-client backpressure test with
  `RUN_SLOW_CLIENT=1`.
- Room capacity and mention notification bookkeeping now follow
  `TNT_MAX_CONNECTIONS` instead of a hidden fixed 64-client array limit.
- Updated the roadmap to reflect completed `tntctl`, stable exec contract, and
  monitoring-interface work, leaving the remaining daemon naming and runtime
  queue work explicit.
- Strict release preflight now builds and installs from the local `vX.Y.Z` tag
  source archive, catching untracked files that would be missing from a GitHub
  source release.
- Release documentation now creates the local tag before strict release checks,
  matching the strict gate's tag-at-HEAD requirement.
- Startup option parsing now reports missing values for `--bind`, `-p`,
  `--idle-timeout`, and related flags with the localized
  "option requires argument" diagnostic instead of treating the option as
  unknown.
- `tntctl` now reuses the SSH exec command matcher for local command
  validation, so `tntctl host --help` reaches the server-side exec help alias
  instead of being rejected locally.
- `tntctl` local help and local validation errors now follow `TNT_LANG` and
  locale selection, matching the server CLI's i18n behavior.
- Arch and Debian packaging drafts now create the `tnt` system user used by
  the packaged systemd unit, and release preflight checks that metadata.
- The Homebrew formula draft now defines a `brew services` entry that runs the
  installed `tnt` binary with state under `var/tnt`.
- Added `scripts/package_debian_source.sh` and `make debian-source-package`
  to assemble Debian/Ubuntu source-package trees from the current project
  without publishing or uploading anything.
- Release preflight now smoke-tests the staged installed `tnt` binary's
  `--log-check` and `--log-recover` modes, catching package artifact drift.
- The i18n helper now supports language-keyed string initializers through
  `I18N_STRING_MAP`, so future languages can be added incrementally without
  changing every existing two-language string initializer.
- Split UI-language parsing from localized text lookup: `src/i18n.c` now owns
  locale/code parsing, while `src/i18n_text.c` owns the table-driven text
  catalog with coverage checks for every message ID.
- Kept command placeholders stable across localized output: Chinese help and
  usage text now uses ASCII metavariables such as `<user>` and `<message>`.
- Standardized user-facing `:msg` / `:inbox` terminology around "private
  message" / "私信" instead of mixing it with "whisper" wording.
- Kept localized startup CLI syntax stable by using `用法: tnt [options]`
  instead of localizing the `[options]` metavariable.
- Moved SSH exec help rows into an `exec_catalog` module so command metadata
  no longer lives as one large translated blob inside the shared i18n table.
- Refreshed contributor and development guidance so new commands are added
  through `command_catalog`, `exec_catalog`, and `i18n_text` instead of stale
  `ssh_server.c` / inline-`strcmp` instructions.
- Refreshed developer ownership guidance to match the current update-sequence
  model: room broadcasts update shared state only, while each interactive
  client renders and flushes its own SSH channel.
- `exec_catalog` now owns SSH exec command matching as well as help metadata,
  reducing duplicate command knowledge in `src/exec.c`.
- Replaced hard-coded `chat.m1ng.space` examples with `chat.example.com` so
  public documentation does not imply a specific production host.
- First-run connection examples now use `localhost`, keeping
  `chat.example.com` for deployed public-host examples.
- Moved SSH exec usage text and argument-shape checks into `exec_catalog`, so
  `src/exec.c` no longer duplicates `--json` and required-message validation.
- Moved interactive command usage text and first-pass argument-shape checks
  into `command_catalog`, so known commands with bad arguments now show usage
  instead of unknown-command guidance.
- Renamed the internal language state from help-oriented names to
  UI-language names (`ui_lang_t`, `client->ui_lang`, and
  `i18n_*_ui_lang`) so future i18n work has a correctly named seam.
- Command names, aliases, help summaries, concise-manual command rows, and
  unknown-command suggestions now share a dedicated `command_catalog` module.
- COMMAND-mode output is now a small scrollable pager with `j/k`, page
  movement, `g/G`, and `q`/Esc close controls, so long `:last` and `:search`
  results are readable instead of being cut off by the terminal height.
- Collapsed the interactive help surface around a concise Unix-style `:help`
  manual and the `?` full key reference; `:support` is no longer a user-facing
  command.
- First-use hints and unknown-command guidance now point users to `:help`
  instead of the removed support entry.
- The concise manual module is now named `manual_text`, and the redundant
  interactive `:commands` entrypoint was removed.
- The concise `:help` manual now stays within one command-output screen so it
  does not truncate on normal terminal sizes.
- Language selection is limited to stable codes (`en`, `zh`) and
  locale-shaped environment values; natural-language labels are not accepted
  as command arguments.
- Full-screen help now uses `l` to cycle the UI language through the i18n
  module instead of hard-coding one key per language.
- Language parsing, language-code output, and help-language cycling now share
  one internal language registry.
- Removed the unused `MODE_HELP` enum value and refreshed development-guide
  module descriptions for the split between language parsing and text lookup.
- `i18n_text` now indexes localized strings by `UI_LANG_*` instead of storing
  English/Chinese as hard-coded struct fields.
- `command_catalog` now uses the shared localized-string helper for help,
  manual, and usage text instead of per-field English/Chinese members.
- `exec_catalog` now uses the same localized-string helper for exec help
  summaries.
- Startup CLI help and option error formats now use the shared
  localized-string helper and English fallback path.
- The concise `:help` manual text now uses the shared localized-string helper
  around the command-catalog rows.
- The full-screen key reference now uses the shared localized-string helper
  around the command-catalog rows.
- SSH exec help headers and usage prefixes now use the shared
  localized-string helper and English fallback path.
- Documented i18n and user-facing text rules for English-first source text,
  stable command syntax, concise help copy, and translation-only localization.
- Rewrote the quick setup guide as a concise English-first user lifecycle
  document with a short Chinese notes section.
- The shared UI text catalog now uses the same localized-string initializer
  as the smaller text modules, avoiding GCC missing-braces warnings.

## 1.0.1 - 2026-05-24 - Release candidate hardening

### Added
- Added a first i18n boundary: `TNT_LANG` / locale detection now chooses the
  default interactive UI language (`en` or `zh`) for username prompts, status
  hints, help output, and `:support`.
- Added `:lang <en|zh>` so users can switch the interactive UI language for
  their current session.
- COMMAND-mode `:help`, unknown-command guidance, language command output, and
  continuation prompts now follow the session UI language.
- The full-screen help title and footer now follow the session UI language,
  with UTF-8-aware title padding for Chinese.
- Common COMMAND-mode outputs now respect the session language, including
  `:users` headers and `:mute-joins` state text.
- Command-output and MOTD screen chrome now use the session UI language.
- Common command usage errors now stay in the session language, and bare
  `:search`, `:msg`, and `:nick` show usage instead of falling through to
  unknown-command guidance.
- Command output text for common interactive commands is now centralized in
  the i18n table instead of being scattered through command flow logic.
- TUI title-bar status labels, including online count, mute marker, and help
  hint, now follow the session UI language.
- Join, leave, and nickname-change system messages now use a dedicated
  `system_message` module, follow the sender's session language, and keep
  `:mute-joins` filtering compatible with both Chinese and English logs.
- The interactive welcome screen now follows the selected UI language,
  including the narrow-terminal fallback.
- Full-screen help and COMMAND-mode help now live in a dedicated `help_text`
  module, keeping large bilingual help copy out of TUI and command flow code.
- Session language, unknown-command, suggestion, and continuation prompts now
  use the shared i18n table instead of inline command-flow conditionals.
- Interactive and exec support guide copy now lives in a dedicated
  `support_text` module, with focused language-selection unit coverage.
- Exec-mode help, usage errors, unknown-command feedback, and post validation
  messages now follow `TNT_LANG` while preserving stable machine-readable
  command output.
- Startup CLI help and option errors now live in a dedicated `cli_text` module
  and follow `TNT_LANG` / locale for English and Chinese users.
- Idle-timeout disconnect notices now follow the session UI language.

### Changed
- `make test` now fails on integration-test regressions; constrained local
  environments can use `make test-advisory` for the previous advisory behavior.
- Removed the duplicate `deploy.yml` CI workflow so automated checks stay
  focused on CI while production deployment remains manual.
- Fixed the per-IP connection-rate limit to allow the configured number of
  attempts before blocking, added unit coverage, and exposed
  `make connection-limit-test` for the black-box limit regression test.
- Security feature checks now use isolated ports and temporary state
  directories, so they no longer require `timeout`/`gtimeout` or write
  `host_key` / `messages.log` into the test directory.
- Added `make security-test` and `make ci-test` so local runs can use the same
  full verification path as GitHub Actions.
- Anonymous access checks now use isolated state, wait for real SSH health,
  avoid external `timeout` helpers, and run through `make anonymous-access-test`
  as part of `make ci-test`.
- Stress testing now uses isolated state, waits for real SSH health, avoids
  external `timeout` helpers, and is available through `make stress-test`.
- Basic integration tests now wait for real SSH `health` responses instead of
  sleeping for a fixed startup delay.
- Connection-limit tests now use shared SSH health readiness checks for both
  concurrent-session and connection-rate scenarios.
- CI memory-leak smoke checks now use an isolated state directory, wait for
  real SSH readiness, and clean up the exact server PID instead of `pkill`.
- CI memory-leak smoke checks now pre-generate the host key and use a longer
  valgrind readiness window, avoiding false failures during slow startup.
- Language parsing now tolerates surrounding whitespace and accepts the
  `english` alias, improving `TNT_LANG` and `:lang` ergonomics.
- Refreshed the development guide's command/keybinding instructions so they
  point at the current modular `commands`, `exec`, `i18n`, and help text files.
- Refreshed README and quick-reference module maps to match the current
  `cli_text`, `help_text`, `support_text`, i18n, exec, and rate-limit modules.
- NORMAL mode now opens at the latest visible messages instead of the oldest
  in-memory message. Use `k`/PageUp to browse older history and `G`/End to
  return to the latest messages.
- NORMAL mode status now shows the visible message range and points users to
  `G latest` when new messages arrive while they are browsing.
- NORMAL mode now keeps following the latest messages while the view is pinned
  to the bottom; scrolling upward switches into history browsing.
- NORMAL mode now accepts arrow keys, PageUp/PageDown, and Home/End in addition
  to the existing Vim-style keys.
- Message viewport and scroll-state rules now live in a focused
  `history_view` module instead of being split across input and rendering code.
- Added unit coverage for `history_view` scroll boundaries, live-follow state,
  and date-divider-aware latest windows.
- Status/input line rendering now lives in a focused `tui_status` module,
  keeping the main TUI renderer closer to layout orchestration.
- Added `:support` / `support` quick guides so interactive users and SSH exec
  clients can discover common actions and troubleshooting paths in-product.
- The GitHub workflow formerly named deploy now runs CI only; production
  deployment remains a manual operator action.
- Command output rendering now truncates ANSI-styled UTF-8 text without
  counting escape sequences as visible width or cutting color codes.
- Host-key generation now uses the non-deprecated libssh PKI API on libssh
  0.12+ while keeping compatibility with older libssh releases.
- INSERT mode now shows a lightweight first-use hint for sending, browsing,
  and `:support`.
- `:support` is now task-oriented around common user goals, and mistyped
  commands suggest the nearest known command when possible.
- Added a local `make release-check` preflight for release/package validation
  without tagging, publishing, or deploying.
- CI now installs `expect` on Ubuntu so interactive integration tests run
  instead of being skipped, and runs `make release-check` on every push/PR.
- The tag-triggered release workflow now builds on native x64/arm64 runners,
  verifies artifact architecture, emits one checksum file, and creates a draft
  release for manual review instead of publishing immediately.
- The one-line installer now downloads `checksums.txt`, verifies the selected
  binary before installation, and fails fast on missing release assets.
- Added a Debian packaging metadata draft for the future Ubuntu PPA path, with
  lightweight validation in `make release-check`.
- Added an Arch `.SRCINFO` draft and AUR maintainer notes, with version/package
  checks in `make release-check`.
- Added Homebrew tap maintainer notes, and expanded `make release-check` to
  validate the formula class and `libssh` dependency.

## 2026-05-18 - Interactive input polish

### Added
- Bracketed paste handling keeps multi-line pasted text in the input buffer
  until the user presses Enter, then sends it as one message.
- Input and paste overflow now rings the terminal bell when the 1023-byte
  message limit is reached.
- Added an interactive `expect` regression test for basic TTY input,
  bracketed paste, and overlong paste capping.
- Added the exec-mode regression test to the main `make test` path.

### Fixed
- SSH exec clients now survive stdin EOF long enough to flush stdout, exit
  status, EOF, and channel close. This fixes non-interactive commands such as
  `ssh localhost health` and `ssh user@host post "message"`.

## 2026-05-16 - Internal cleanup

### Fixed
- `message_load()` now holds `g_message_file_lock` for the duration of the read.
  Previously `:last [N]` could race with `message_save()` and observe a
  half-written line.
- `constant_time_strcmp()` accumulates the length difference in `size_t` instead
  of `unsigned char`. The old code lost the length-mismatch signal when the
  two lengths differed by a multiple of 256.

### Changed
- `buffer_appendf()` and `buffer_append_bytes()` moved to `common.c`; the two
  identical copies in `ssh_server.c` and `tui.c` have been removed.
- Removed `TODO.md` (both items completed) and `docs/README.old` (superseded by
  the root `README.md`).
- Trimmed the auto-generated 2025 entry block from this changelog.

## 2026-04-23 - Chat UX Commands and MOTD

### Added
- **`:last [N]`** — show last N messages retrieved directly from the log file (1–50, default 10), bypassing the 100-message in-memory ring buffer limit
- **`:search <keyword>`** — case-insensitive full-text search across the entire message history on disk; returns the most recent 15 matches
- **`:mute-joins`** — per-client toggle to silence join/leave system notifications; title bar shows `[静音]` when active
- **MOTD support** — place `motd.txt` in the state directory; users see it on connect and press any key to enter chat
- **`message_search()`** — new function in `message.c` / `message.h` for log file keyword search with rolling result collection
- Updated in-TUI help screens (English and Chinese) with new commands

## 2026-03-10 - SSH Runtime & Unix Interface Update

### Fixed
- moved SSH handshake/auth/channel setup out of the main accept loop
- replaced synchronous room-wide fan-out with room update sequencing and per-client refresh
- switched idle session handling to `ssh_channel_poll_timeout()` plus blocking reads so quiet sessions are not dropped incorrectly
- made `-d/--state-dir` create the runtime state directory automatically

### Added
- SSH exec commands: `help`, `health`, `users`, `stats --json`, `tail`, `post`
- PTY window-change handling for terminal resize
- `TNT_MAX_CONN_RATE_PER_IP` for per-IP connection-rate control
- `tests/test_exec_mode.sh`
- `tests/test_connection_limits.sh`

### Changed
- `TNT_MAX_CONN_PER_IP` now means concurrent sessions per IP
- stress tests now disable rate-based blocking so they exercise concurrency instead of self-throttling

## 2026-01-22 - Security Audit Fixes

Comprehensive security hardening addressing 23 identified vulnerabilities across 6 categories.

### Critical

- **[AUTH]** Add optional access token authentication (`TNT_ACCESS_TOKEN`)
- **[AUTH]** Implement IP-based rate limiting (10 conn/IP/60s, 5-min block after 5 auth failures)
- **[AUTH]** Add global connection limits (default: 64, configurable via `TNT_MAX_CONNECTIONS`)

### High Priority

- **[BUFFER]** Replace all `strcpy()` with `strncpy()` (3 locations)
- **[BUFFER]** Add buffer overflow checking in `client_printf()`
- **[BUFFER]** Implement UTF-8 validation to prevent malformed input and overlong encodings
- **[SSH]** Upgrade RSA key from 2048 to 4096 bits
- **[SSH]** Fix key file permission race with atomic generation (umask + temp file + rename)
- **[SSH]** Add configurable bind address (`TNT_BIND_ADDR`) and log level (`TNT_SSH_LOG_LEVEL`)
- **[CONCURRENCY]** Fix `room_broadcast()` reference counting race
- **[CONCURRENCY]** Fix `tui_render_screen()` message array TOCTOU via snapshot approach
- **[CONCURRENCY]** Fix `handle_key()` scroll position TOCTOU

### Medium Priority

- **[INPUT]** Add username validation rejecting shell metacharacters and control chars
- **[INPUT]** Sanitize message content to prevent log injection attacks
- **[INPUT]** Enhance `message_load()` with field length and timestamp validation
- **[RESOURCE]** Convert message position array from fixed 1000 to dynamic allocation
- **[RESOURCE]** Enhance `setup_host_key()` validation (size, permissions, auto-regeneration)
- **[RESOURCE]** Improve thread cleanup with proper pthread_attr and error handling

### New Environment Variables

- `TNT_ACCESS_TOKEN` - Optional password for authentication (backward compatible)
- `TNT_BIND_ADDR` - Bind address (default: 0.0.0.0)
- `TNT_SSH_LOG_LEVEL` - SSH logging verbosity 0-4 (default: 1)
- `TNT_RATE_LIMIT` - Enable/disable rate limiting (default: 1)
- `TNT_MAX_CONNECTIONS` - Max concurrent connections (default: 64)
- `TNT_MAX_CONN_PER_IP` - Max connections per IP (default: 5)

### Security Summary

| Category | Fixes | Impact |
|----------|-------|--------|
| Buffer Security | 3 | Prevents overflows, malformed UTF-8 |
| SSH Hardening | 4 | Stronger crypto, no races |
| Input Validation | 3 | Prevents injection, log poisoning |
| Resource Management | 3 | Handles large logs, prevents DoS |
| Authentication | 3 | Optional protection, rate limiting |
| Concurrency Safety | 3 | Eliminates races, crashes |
| **TOTAL** | **19** | **23 vulnerabilities fixed** |

All changes maintain backward compatibility. Server remains open by default.

---

## 2025-12-02 - Stability & Testing Update

### Fixed
- Double colon bug in vim command mode (`:` key consumed properly)
- strtok data corruption in command output rendering
- Use-after-free race condition (added reference counting)
- SSH read blocking issues (added timeouts)
- PTY request infinite loop
- Message history memory waste (optimized loading)

### Added
- Reference counting for thread-safe client cleanup
- SSH read timeout (30s) and error handling
- UTF-8 incomplete sequence detection
- AddressSanitizer build target (`make asan`)
- Basic functional tests (`test_basic.sh`)
- Stress testing script (`test_stress.sh`)
- Static analysis target (`make check`)
- Developer documentation (HACKING)

### Changed
- Improved error handling throughout
- Better memory management in message loading
