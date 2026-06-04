# Roadmap

TNT is moving toward a durable Unix-style utility: a small, predictable tool with a stable interface, explicit configuration, scriptable output, and operationally simple deployment.

This roadmap is intentionally strict. Each stage should leave the project easier to reason about, easier to automate, and safer to operate.

## Design Principles

- Keep the default path simple: install, run, connect.
- Treat non-interactive interfaces as first-class, not as an afterthought to the TUI.
- Prefer explicit flags, stable exit codes, and machine-readable output over implicit behavior.
- Keep daemon concerns separate from control-plane concerns.
- Make failure modes observable and testable.
- Preserve the Vim-style interactive experience without coupling it to core server semantics.

## Stage 1: Interface Contract

Goal: make TNT predictable for operators, scripts, and package maintainers.

- ✅ introduce `tntctl` as a thin control client over the stable SSH exec surface
- keep SSH exec support, but treat it as a transport for stable commands rather
  than an ad hoc command surface
- ✅ define stable subcommands and exit codes for:
  - `health`
  - `stats`
  - `users`
  - `tail`
  - `dump`
  - `post`
- ✅ support text and JSON output modes where machine use is likely
- ✅ normalize command parsing, help text, and error reporting
- ✅ keep `tnt` as the 1.x server binary; reserve any future `tntd` split for a
  major-version compatibility plan
- ✅ add `--bind`, `--port`, `--state-dir`, `--public-host`,
  `--max-connections`, and related long options consistently
- ✅ add man pages for `tnt` and `tntctl`

## Stage 2: Runtime Model

Goal: make long-running operation boring and reliable.

- ✅ move session callback ownership into `client.c` and release sessions
  through one `client_release_session()` path
- ✅ remove cross-client SSH channel writes from mention and private-message
  notifications
- continue replacing ad hoc cross-thread UI mutation with per-client event
  delivery where new features need cross-client notifications
- ✅ add bounded outbound queues so closed SSH windows cannot immediately stall
  interactive output writes
- separate accept, session bootstrap, interactive I/O, and persistence concerns more cleanly
- ✅ make room/client capacity fully runtime-configurable with no hidden
  compile-time ceiling
- ✅ document hard guarantees and soft limits

## Stage 3: Data and Persistence

Goal: make stored history durable, inspectable, and recoverable.

- ✅ formalize the message log v1 format
- ✅ keep persisted timestamps in UTC throughout write and replay
- ✅ validate persisted UTF-8 and record structure before replay/search
- ✅ provide an inspection/export command for persisted records
- ✅ add log rotation and compaction tooling
- ✅ define broader recovery tooling for truncated or partially corrupted logs

## Stage 4: Interactive UX

Goal: keep the interface efficient for terminal users without sacrificing simplicity.

- ✅ keep the current modal editing model precise and documented
- ✅ support resize, command history, pager navigation, and predictable paste
  behavior
- add in-line cursor movement/editing only if it can stay simple and testable
- add useful chat commands with clear semantics:
  - ✅ `:nick` / `:name` — nickname change with broadcast
  - ✅ `/me` — action messages
  - ✅ `:last N` — show last N messages from disk history
  - ✅ `:search <keyword>` — case-insensitive full-text search
  - ✅ `:mute-joins` — per-client join/leave notification toggle
- ✅ improve discoverability of NORMAL and COMMAND mode actions
- ✅ make status lines and help output concise enough for small terminals

## Stage 4.5: Module Foundation

Goal: let community features plug into TNT without coupling every user request
to the core server binary.

- keep TNT core basic and broadly compatible; route personalized workflows,
  rich visuals, and terminal-specific experience upgrades through modules
- define the external-process module protocol before loading any third-party
  code into production rooms
- keep module messages compatible with plain terminal clients by requiring
  plain-text fallbacks for rich content and attachments
- treat terminal image protocols as optional renderer capabilities, not as the
  core message format
- prefer JSON Lines over stdin/stdout for early modules so TNT can supervise,
  restart, rate-limit, and disable modules independently
- keep module permissions explicit: message read/create, command registration,
  private-message access, and future attachment access must be separate grants
- publish official examples in a companion community repository that tracks
  TNT protocol versions and license terms

## Stage 5: Operations and Security

Goal: make public deployment manageable.

- ✅ provide clear distinction between concurrent session limits and
  connection-rate limits
- add admin-only controls for read-only mode, mute, and ban
- ✅ expose a minimal health and stats surface suitable for monitoring
- support systemd-friendly readiness and watchdog behavior
- ✅ document recommended production defaults for public, private, and
  localhost-only deployments
- tighten CI around authentication, limits, and restart behavior

## Stage 6: Release Quality

Goal: make regressions harder to introduce.

- expand CI coverage across Linux and macOS for build and smoke tests
- add sanitizer jobs and targeted fuzzing for UTF-8, log parsing, and command parsing
- ✅ add a configurable soak test for idle sessions, reconnects, and control
  interface availability
- ✅ add deeper slow-client coverage with a deliberately backpressured SSH
  client
- ✅ verify staged package installs, systemd unit paths, packaging metadata,
  Debian source assembly, Homebrew service metadata, and installed log
  maintenance modes in release preflight
- keep deployment and test docs aligned with actual runtime behavior
- require every user-visible interface change to update docs and tests in the same change set

## Immediate Next Tasks

These are the next changes that should happen before new feature work expands the surface area.

1. Replace remaining source-archive checksum placeholders only after the
   explicit release source archive exists, then run `make package-publish-check`.
2. Create or move the `vX.Y.Z` tag only when the release commit is final, then
   run `make release-check-strict` before pushing it.
3. Decide whether admin-only moderation controls belong in 1.0.x or should
   wait for a later minor release.
