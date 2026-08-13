# Performance Contract

TNT treats performance as a compatibility property. New features must keep
queues, retries, caches, and concurrency bounded, and a change that crosses a
redline must include measurements, an explanation, and a recovery plan.

The reference target is a low-end Linux host with 1 vCPU, 128 MiB RAM, and no
swap. The target load is 64 fully joined interactive sessions with short bursts
of 1,000 messages per second. Correctness, security, privacy, accessibility, and
maintainability take precedence over a faster number.

## Budgets

| Metric | Ideal | Regression redline |
|---|---:|---:|
| Existing-key process startup p95 | 20 ms | 50 ms |
| Idle server RSS | 8 MiB | 16 MiB |
| 64 joined sessions RSS | 80 MiB | 112 MiB |
| Local SSH health handshake p95 | 50 ms | 100 ms |
| Interactive post-to-render p99 | 5 ms | 20 ms |
| Interactive ingest and persistence | 1,000 msg/s | 500 msg/s |
| Main `tnt` binary | 256 KiB | 512 KiB |

RSS and virtual address space are reported separately. In particular, the
1 MiB configured pthread stack is a reservation per session and must not be
reported as resident memory unless the pages are actually resident.

## Running the benchmark

The benchmark requires Python 3.10 or newer and an OpenSSH `ssh` client; it has
no Python package dependencies. Build TNT first, then run:

```sh
make perf
```

The command creates a timestamped JSON report under the gitignored
`perf-results/` directory and also prints successful results to stdout. Use an
explicit path for automation:

```sh
make perf PERF_OUTPUT=/tmp/tnt-perf.json
```

If a measured scenario or correctness check fails, the same path receives an
atomic diagnostic JSON with commit, environment, workload, and error metadata
instead of disappearing entirely.

Interactive OpenSSH processes use one full-duplex local socket each, and TNT's
directed-signal session wakeup consumes no per-session notification FD. This
keeps the 64-client target runnable under the common macOS soft limit of 128
open file descriptors.

Other entry points are:

```sh
make perf-smoke  # short CI profile; enforce stable redlines
make perf-check  # normal profile; enforce stable redlines
make perf-full   # 64 real sessions; enforce every performance redline
```

The workload is configurable without editing the script:

```sh
make perf \
  PERF_STARTUP_SAMPLES=21 \
  PERF_HANDSHAKE_SAMPLES=21 \
  PERF_FANOUT_SAMPLES=21 \
  PERF_CLIENTS=64 \
  PERF_IDLE_SECONDS=10 \
  PERF_MESSAGES=1000 \
  PERF_HISTORY_RECORDS=100000 \
  PERF_ENFORCE=all
```

Every latency distribution uses at least five samples and reports the raw
samples plus min, mean, p50, p95, p99, and max. Percentiles use the nearest-rank
method. `PERF_ENFORCE=stable` gates existing-key startup, idle RSS, and binary
size. The fresh-process SSH handshake remains recorded but is not a hard
shared-runner gate because host process scheduling dominates its tail.
`PERF_ENFORCE=all` additionally gates the handshake, 64-session RSS when 64
sessions were measured, interactive end-to-end post-to-render latency, and
ingest throughput. An enforced metric that cannot be measured is a failure,
never a silent pass. For that reason, `PERF_ENFORCE=all` requires exactly 64
clients so a larger workload cannot be mislabeled and judged as 64-session RSS.

The extended Linux CI job runs `make perf-smoke`, fails when a stable redline is
crossed, and retains the JSON report for 30 days as a workflow artifact. The
broader metrics remain visible in that report even when they are not part of
the low-variance CI gate.

## Measurement contract

The driver records the Git commit and dirty state, UTC timestamp, OS and kernel,
architecture, CPU, logical CPU count, total memory, compiler, OpenSSH, libssh,
workload configuration, raw samples, and binary sizes.

Scenarios have deliberately narrow definitions:

- First start measures process spawn through the listening announcement and
  includes automatic RSA-4096 host-key generation.
- Existing-key startup repeats that measurement after the key exists.
- History startup repeats it with a valid 100,000-record log by default.
- Handshake starts a fresh OpenSSH process and ends after an authenticated
  `health` response.
- A session counts as joined only after authentication, username submission,
  `room_add_client()`, and emission of the post-join bracketed-paste marker.
  Seeing the username prompt is never counted as success. The final set is
  cross-checked through `users --json`, and resources are sampled after the
  configured idle interval so initial screen setup has settled.
- Interactive post-to-render begins before starting a fresh OpenSSH exec
  `post` and ends when its unique marker is visible in an already joined TUI
  client. It deliberately includes client process startup, SSH authentication,
  command execution, room fan-out, and TUI wake/render. It is an end-to-end
  user-visible latency, not an isolated in-process broadcast benchmark.
- Ingest begins when an interactive client receives an ordered payload and
  ends after every expected record is visible in `messages.log` and the final
  unique message has rendered for all joined sessions. Persistence completeness,
  persistence ordering, and final fan-out are mandatory, not optional throughput
  tradeoffs. The server must remain healthy and every joined session must still
  be present afterward.
- Capacity rejection occupies the configured final slot, times a rejected
  health connection, and verifies that the existing session survives.

The JSON schema is versioned with `schema_version`. Consumers should reject a
new major schema they do not understand instead of silently comparing fields
with changed semantics.

## Coverage boundaries

This first benchmark slice covers startup, large-history replay, sequential
fresh handshakes, fully joined idle sessions, one-observer post-to-render,
ordered interactive ingest, process memory, idle CPU time, binaries, and
capacity rejection.

The following scenarios remain separate or future work:

- `make slow-client-test` remains the backpressure correctness test; latency
  samples under backpressure are not yet part of the JSON report.
- `make soak-test DURATION=1800` remains the 30-minute durability path; the
  default performance command intentionally does not run for half an hour.
- A synchronized connection storm, 64-receiver fan-out distribution, cache-miss
  storage run, and modules-on/modules-off comparison still need benchmark
  scenarios before their numbers can become release gates.
- The benchmark measures one local machine. Results from different hardware or
  operating systems must not be compared without retaining the environment
  metadata and workload configuration.
