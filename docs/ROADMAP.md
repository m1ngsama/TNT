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

- split the current surface into `tntd` (daemon) and `tntctl` (control client)
- keep SSH exec support, but treat it as a transport for stable commands rather than the primary API shape
- define stable subcommands and exit codes for:
  - `health`
  - `stats`
  - `users`
  - `tail`
  - `post`
- support text and JSON output modes where machine use is likely
- normalize command parsing, help text, and error reporting
- add `--bind`, `--port`, `--state-dir`, `--public-host`, `--max-clients`, and related long options consistently
- add a man page for `tntd` and `tntctl`

## Stage 2: Runtime Model

Goal: make long-running operation boring and reliable.

- move client state to a clearer ownership model with one release path
- finish replacing ad hoc cross-thread UI mutation with per-client event delivery
- add bounded outbound queues so slow clients cannot stall other users
- separate accept, session bootstrap, interactive I/O, and persistence concerns more cleanly
- make room/client capacity fully runtime-configurable with no hidden compile-time ceiling
- document hard guarantees and soft limits

## Stage 3: Data and Persistence

Goal: make stored history durable, inspectable, and recoverable.

- formalize the message log format and version it
- keep timestamps in a timezone-safe format throughout write and replay
- validate persisted UTF-8 and record structure before replay
- add log rotation and compaction tooling
- provide an offline inspection/export command
- define recovery behavior for truncated or partially corrupted logs

## Stage 4: Interactive UX

Goal: keep the interface efficient for terminal users without sacrificing simplicity.

- keep the current modal editing model, but make its behavior precise and documented
- support resize, cursor movement, command history, and predictable paste behavior
- add useful chat commands with clear semantics:
  - `/nick`
  - `/me`
  - `/last N`
  - `/search`
  - `/mute-joins`
- improve discoverability of NORMAL and COMMAND mode actions
- make status lines and help output concise enough for small terminals

## Stage 5: Operations and Security

Goal: make public deployment manageable.

- provide clear distinction between concurrent session limits and connection-rate limits
- add admin-only controls for read-only mode, mute, and ban
- expose a minimal health and stats surface suitable for monitoring
- support systemd-friendly readiness and watchdog behavior
- document recommended production defaults for public, private, and localhost-only deployments
- tighten CI around authentication, limits, and restart behavior

## Stage 6: Release Quality

Goal: make regressions harder to introduce.

- expand CI coverage across Linux and macOS for build and smoke tests
- add sanitizer jobs and targeted fuzzing for UTF-8, log parsing, and command parsing
- add soak tests for long-lived sessions and slow-client behavior
- keep deployment and test docs aligned with actual runtime behavior
- require every user-visible interface change to update docs and tests in the same change set

## Immediate Next Tasks

These are the next changes that should happen before new feature work expands the surface area.

1. Introduce `tntctl` and move stable command handling behind it.
2. Define exit codes and JSON schemas for `health`, `stats`, `users`, `tail`, and `post`.
3. Add per-client outbound queues and finish untangling client-state ownership.
4. Remove the remaining hidden runtime limits and make them explicit configuration.
5. Add a long-running soak test that exercises idle sessions, reconnects, and slow consumers.
