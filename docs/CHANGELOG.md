# Changelog

## 2026-05-21 - Message browsing polish

### Added
- Added a first i18n boundary: `TNT_LANG` / locale detection now chooses the
  default interactive UI language (`en` or `zh`) for username prompts, status
  hints, help language, and `:support`.
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

### Changed
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
