# Reviewed Performance Evidence

Measurements belong to one commit, host and workload. JSON is not committed;
it goes to stdout unless an output path is explicitly requested.

## Current 32 MiB core gate

On 2026-08-14, clean commit
`d9116273f5e0fb8bc00542a1f804fa32366dd340` ran on the 2-vCPU low-memory
Linux target. TNT was pinned to one CPU in a 32 MiB, no-swap cgroup while the
OpenSSH load generators used the other CPU. The report confirmed the effective
cgroup values.

| Result | Measured value | Regression redline |
|---|---:|---:|
| Existing-key startup p95 | 25.581 ms | 50 ms |
| Idle RSS | 7,680 KiB | 16,384 KiB |
| 64-session RSS | 17,408 KiB | 32,768 KiB |
| SSH health handshake p95 | 65.978 ms | 100 ms |
| Persisted record to all 63 peer renders p99 | 19.061 ms | 20 ms |
| Ordered, persisted 1,000-message ingest | 525.417 msg/s | 500 msg/s |
| Main binary | 180,008 bytes | 524,288 bytes |

All gates passed, but latency and ingest are close to their redlines. This is a
reason to watch future changes, not to tighten CPU or memory limits further.

## Durability evidence

The already completed long run remains valid evidence for the 30-minute
acceptance criterion. On 2026-08-13, clean commit
`854059fe7db813777a9a2a4af0621588235a5486` ran for 1,800.001 seconds with 64
sessions on the same host class. All sessions joined, survived and sent; all
180 messages persisted in order and all 11,340 expected peer renders were
observed. Peak RSS was 17,664 KiB and post-warmup RSS growth was -2,560 KiB.

The later lean commit removes benchmark telemetry and reuses the full profile's
sessions for optional durability; it does not change message persistence or
session ownership. The expensive 30-minute run was therefore not repeated for
that simplification.

The manual [`Performance charter`](../../.github/workflows/performance.yml)
workflow has no schedule. Successful runs upload nothing by default; failures
or explicitly retained reports expire after three days. The durability phase
is separately opt-in. Module implementation performance is measured in
`tnt-modules`.

Reproduction commands and metric definitions are in
[`docs/PERFORMANCE.md`](../PERFORMANCE.md).
