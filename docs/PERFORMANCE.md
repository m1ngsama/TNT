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
| Persisted message to all other joined TUI receivers p99 | 5 ms | 20 ms |
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
make perf-soak   # 64 functional sessions for 30 minutes
```

The workload is configurable without editing the script:

```sh
make perf \
  PERF_STARTUP_SAMPLES=21 \
  PERF_HANDSHAKE_SAMPLES=21 \
  PERF_FANOUT_SAMPLES=101 \
  PERF_STORM_CLIENTS=64 \
  PERF_CLIENTS=64 \
  PERF_IDLE_SECONDS=10 \
  PERF_MESSAGES=1000 \
  PERF_HISTORY_RECORDS=100000 \
  PERF_ENFORCE=all
```

Use the released tnt-modules checkout for an explicit modules-on/off profile:

```sh
MODULE_PATHS=$(find ../tnt-modules/modules -mindepth 1 -maxdepth 1 \
  -type d -name '*-module' -print | sort | paste -sd: -)
make perf-full PERF_MODULE_PATHS="$MODULE_PATHS"
```

The long durability profile is separate so an ordinary benchmark never hides a
half-hour run:

```sh
make perf-soak PERF_SOAK_OUTPUT=/tmp/tnt-perf-soak.json
```

It defaults to 64 fully joined sessions for 1,800 seconds. Senders rotate across
all sessions; every marker must be persisted in order and rendered by all 63
peers. The test continuously verifies process/session survival, samples RSS and
virtual memory, and fails above 112 MiB peak RSS or 16 MiB post-warmup RSS growth.
Use `make perf-soak-smoke` only to verify the driver contract, never as durability
evidence.

On a dedicated Linux reference host, constrain only the TNT server (not the
OpenSSH load generators) and record the constraint in the report:

```sh
make perf-full \
  PERF_SERVER_WRAPPER='systemd-run --user --scope --quiet -p AllowedCPUs=0 -p MemoryMax=128M -p MemorySwapMax=0'
make perf-soak \
  PERF_SOAK_SERVER_WRAPPER='systemd-run --user --scope --quiet -p AllowedCPUs=0 -p MemoryMax=128M -p MemorySwapMax=0'
```

This transient user scope constrains TNT and its module children as one cgroup
to CPU 0, 128 MiB of memory, and zero swap. Use it only on a dedicated host
where CPU 0 belongs to the test and the user systemd manager supports these
properties. The wrapper is stored verbatim in JSON. It does not constrain the
benchmark driver or its OpenSSH client processes.

On a two-CPU measurement host, the driver and its OpenSSH children can be kept
off the server CPU with `taskset -c 1 make perf-full ...`. Reports record the
driver's effective CPU affinity plus the TNT process's allowed CPU list,
effective cgroup cpuset, memory limit, and swap limit. These fields distinguish
an enforced target profile from a wrapper string that the host ignored.

Every latency distribution uses at least five samples and reports the raw
samples plus min, mean, p50, p95, p99, and max. Percentiles use the nearest-rank
method. The full target profile uses 101 distribution samples so nearest-rank
p99 does not collapse to a single maximum. `PERF_ENFORCE=stable` gates
existing-key startup, idle RSS, and binary
size. The fresh-process SSH handshake remains recorded but is not a hard
shared-runner gate because host process scheduling dominates its tail.
`PERF_ENFORCE=all` additionally gates the handshake, 64-session RSS when 64
sessions were measured, persisted-to-all-peer distribution latency, and ingest
throughput. An enforced metric that cannot be measured is a failure, never a
silent pass. For that reason, `PERF_ENFORCE=all` requires exactly 64 clients so
a larger workload cannot be mislabeled and judged as 64-session RSS.

The extended Linux CI job runs `make perf-smoke`, fails when a stable redline is
crossed, and retains the JSON report for 30 days as a workflow artifact. The
weekly/manual Performance Charter workflow runs the complete 64-session budget
gate with the pinned tnt-modules release and then the 30-minute functional
durability gate. Its two reports are retained together for 90 days. Failed
scenarios retain structured diagnostics rather than disappearing.

## Measurement contract

The driver records the Git commit and dirty state, UTC timestamp, OS and kernel,
architecture, CPU, logical CPU count, driver affinity, total memory, compiler,
OpenSSH, libssh, effective server cgroup limits, workload configuration, raw
samples, and binary sizes.

Scenarios have deliberately narrow definitions:

- First start measures process spawn through the listening announcement and
  includes automatic RSA-4096 host-key generation.
- Existing-key startup repeats that measurement after the key exists.
- History startup repeats it with a valid 100,000-record log by default. A
  separate first-open measurement asks the OS to evict that file from cache via
  `posix_fadvise(POSIX_FADV_DONTNEED)` where available and records whether the
  request was supported. The benchmark never drops global host caches.
- Handshake starts a fresh OpenSSH process and ends after an authenticated
  `health` response.
- Connection storm releases the configured number of OpenSSH clients from a
  thread barrier together. Every client must pass authentication, submit a
  username, complete `room_add_client()`, emit the post-join marker, and appear
  in `users --json`; prompt visibility is not success.
- A session counts as joined only after authentication, username submission,
  `room_add_client()`, and emission of the post-join bracketed-paste marker.
  Seeing the username prompt is never counted as success. The final set is
  cross-checked through `users --json`, and resources are sampled after the
  configured idle interval so initial screen setup has settled.
- Interactive distribution uses one existing joined session as sender. TNT
  flushes the message record before calling `room_broadcast()`, so the gated
  interval begins when the driver first observes that unique record in the log
  and ends when all other 63 joined TUI sessions have rendered it. The report
  also records submit-to-persistence and submit-to-all-peer distributions.
  Filesystem polling, receiver polling, and driver scheduling are included, so
  this remains a conservative external measurement rather than an in-process
  timestamp that changes production behavior.
- Fresh exec post-to-all-receivers remains recorded as a broader, ungated
  end-to-end diagnostic. It starts before a new OpenSSH `post` process and ends
  after every joined TUI renders the exactly-once persisted marker, therefore
  including process creation and SSH authentication. It must not be compared
  with the 20 ms server distribution redline on hosts whose handshake alone is
  slower than that budget.
- Ingest begins when an interactive client receives an ordered payload and
  ends after every expected record is visible in `messages.log` and the final
  unique message has rendered for all joined sessions. Persistence completeness,
  persistence ordering, and final fan-out are mandatory, not optional throughput
  tradeoffs. The server must remain healthy and every joined session must still
  be present afterward.
- Capacity rejection occupies the configured final slot, times a rejected
  health connection, verifies that the existing session survives, and requires
  an explicit operator-log reason. Capacity is enforced before SSH key exchange
  to protect resources, so a stock client can only receive a fast transport
  close at that stage; pretending that an application message can be delivered
  before SSH exists would weaken the protection and is intentionally rejected.
- Slow-client coverage keeps one fully joined TUI unread with a small socket
  buffer, fills its input/output path, and reports health plus an independent
  joined receiver's latency. The unread client may remain bounded or be
  disconnected; it must never block unrelated progress.
- Module comparison repeats an identical ordered ingest workload with modules
  disabled and with every explicitly supplied module enabled. It records module
  manifests/repository commits, process-tree RSS/virtual memory, throughput
  ratio, correctness, survival, and runtime errors. No implicit module path or
  production configuration is inherited.

The JSON schema is versioned with `schema_version`. Consumers should reject a
new major schema they do not understand instead of silently comparing fields
with changed semantics.

## Coverage boundaries

The benchmark now covers startup, best-effort per-file cold history opening,
large-history warm replay, sequential handshakes, synchronized connection
storms, fully joined idle sessions, all-receiver fan-out, ordered ingest,
slow-client backpressure, optional modules-on/off comparison, process and
process-tree memory, idle CPU, binaries, capacity rejection, and a separate
30-minute 64-session functional durability profile.

Two boundaries are deliberate:

- Global cache eviction is not automated because it needs elevated privileges,
  perturbs unrelated workloads, and is unsafe on shared CI/production hosts.
  The report records the portable per-file eviction attempt instead.
- A result belongs to the machine and workload recorded in its JSON. Do not
  compare different hardware or operating systems as one baseline, and do not
  describe a GitHub-hosted runner as the 1-vCPU/128-MiB reference host.

Reviewed reports are indexed under [`docs/performance/`](performance/). The
workflow artifacts remain the evidence for newer commits until another baseline
is deliberately reviewed and checked in.
