# Performance Contract

Performance is a compatibility property, not a reason to make TNT larger. The
reference workload is 64 joined SSH sessions on a 1-vCPU, no-swap Linux host.
The host used for the reviewed run had 128 MiB RAM, but TNT's own 64-session
resident-memory redline is 32 MiB.

Correctness, security and a small Unix-style core take priority over a better
benchmark number. Optional module implementations and their performance tests
belong in the companion `tnt-modules` repository.

## Budgets

| Metric | Ideal | Regression redline |
|---|---:|---:|
| Existing-key startup p95 | 20 ms | 50 ms |
| Idle server RSS | 8 MiB | 16 MiB |
| 64 joined sessions RSS | 24 MiB | 32 MiB |
| Local SSH health handshake p95 | 50 ms | 100 ms |
| Persisted message to all other joined receivers p99 | 5 ms | 20 ms |
| Ordered interactive ingest | 1,000 msg/s | 500 msg/s |
| Main `tnt` binary | 256 KiB | 512 KiB |

RSS and virtual memory are separate. A configured thread-stack reservation is
not resident memory until its pages are used.

## Commands

The driver needs Python 3.10+ and OpenSSH, but no Python packages:

```sh
make perf        # normal local measurement; no gate by default
make perf-smoke  # short stable-budget gate
make perf-check  # normal stable-budget gate
make perf-full   # 64 sessions and every redline
```

By default JSON goes to stdout, so it composes with normal Unix tools and does
not create hidden files. Choose an explicit temporary path when needed:

```sh
make perf-full PERF_OUTPUT=/tmp/tnt-perf.json
```

Each report records the commit and dirty state, host/toolchain, workload,
sample counts, min/mean/p50/p95/p99/max, RSS and binary sizes. Raw sample arrays
are intentionally omitted: they added storage without improving the regression
decision. A failed scenario writes compact diagnostic JSON to the requested
path.

The separate durability profile exists only for release or explicitly reviewed
performance work:

```sh
make perf-soak PERF_OUTPUT=/tmp/tnt-soak.json
```

Its default keeps 64 real sessions joined for 1,800 seconds. Senders rotate;
every marker must be persisted in order and rendered by all 63 peers. The
report retains aggregate memory values rather than every resource sample and
fails above 32 MiB peak RSS or 8 MiB post-warmup RSS growth.

## Cost policy

- Ordinary pushes and pull requests never run performance workloads.
- The Performance Charter workflow has no schedule and runs only by manual
  dispatch.
- A normal manual run executes the full core profile but does not upload an
  artifact. Reports are uploaded for three days only after a failure or when
  the operator explicitly requests retention.
- The 30-minute durability profile is separately opt-in. A previous valid run
  is not repeated for documentation-only or unrelated changes.
- Production is never used as a continuous benchmark target.

This keeps runner, artifact and server costs proportional to an active
performance decision.

## Target-host constraints

On a dedicated Linux host, an optional wrapper can constrain only the TNT
server while leaving the OpenSSH load generators outside the limit:

```sh
taskset -c 1 make perf-full \
  PERF_SERVER_WRAPPER='taskset -c 0 systemd-run --user --scope --quiet -p AllowedCPUs=0 -p MemoryMax=32M -p MemorySwapMax=0' \
  PERF_OUTPUT=/tmp/tnt-perf.json
```

The wrapper and the effective Linux affinity/cgroup values are recorded in the
report. Inspect them before treating a run as constrained evidence. Global
cache eviction is never automated because it disturbs unrelated workloads.

## Measurement contract

- At least five warm samples are used for every latency distribution. The full
  fan-out profile uses 101 measured samples and five unrecorded warmups.
- Startup ends at TNT's listening announcement. First start, existing-key start
  and 100,000-record history opening are reported separately.
- Handshake starts a fresh OpenSSH process and ends at an authenticated
  `health` response.
- A session counts only after authentication, username submission, room entry,
  TNT's post-join marker and confirmation through `users --json`. Seeing a
  username prompt is not success.
- Connection storm clients start together and must all satisfy that join
  contract.
- Fan-out uses an existing joined sender. The gated interval begins when the
  driver first observes the flushed persisted record and ends when every other
  joined TUI renders it. No benchmark-only counter or lock exists in TNT.
- Ingest requires every record to persist in order, the final record to render
  in every session, every session to survive and `health` to remain responsive.
- One unread joined client exercises bounded backpressure while health and a
  responsive peer continue to make progress.
- Capacity testing fills the configured slots, verifies a fast rejection and
  confirms that the existing session survives. Before SSH key exchange, the
  client can only receive a transport close; the explicit reason is available
  to the operator log.

`PERF_ENFORCE=stable` gates startup, idle RSS and binary size.
`PERF_ENFORCE=all` additionally requires exactly 64 clients and gates handshake,
64-session RSS, fan-out and ingest. A missing enforced metric fails.

TNT core integration still validates the module protocol and supervisor. Run
module implementation performance from the companion repository instead:

```sh
make -C ../tnt-modules perf-check
```

## Evidence

The concise reviewed baseline is in
[`docs/performance/README.md`](performance/README.md). A result belongs only to
its recorded commit, host and workload. Raw JSON stays local or in a short-lived
explicit artifact; it is not committed to Git history.
