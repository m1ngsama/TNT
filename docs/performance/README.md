# Reviewed Performance Evidence

Performance reports are evidence for one commit and host, not timeless
promises. JSON goes to stdout unless an output path is explicit and is not
committed to permanent Git history.

Both reports were produced on 2026-08-13 from clean commit
`854059fe7db813777a9a2a4af0621588235a5486` with TNT pinned to one CPU and a
128 MiB, no-swap cgroup. TNT stayed far below that host limit:

| Result | Measured value | Regression redline |
|---|---:|---:|
| Existing-key startup p95 | 25.488 ms | 50 ms |
| Idle RSS | 7,680 KiB | 16,384 KiB |
| 64-session RSS | 16,128 KiB | 32,768 KiB |
| SSH health handshake p95 | 70.406 ms | 100 ms |
| Persisted record to all 63 peer renders p99 | 12.254 ms | 20 ms |
| Ordered, persisted 1,000-message ingest | 1,800.039 msg/s | 500 msg/s |
| Main binary | 180,256 bytes | 524,288 bytes |

The durability profile ran for 1,800.001 seconds. All 64 sessions joined,
survived, and took a turn sending; all 180 messages were persisted in order and
all 11,340 expected peer renders were observed. Peak RSS was 17,664 KiB and
RSS growth was -2,560 KiB. Every machine-readable acceptance field is `true`.

The manual [`Performance charter`](../../.github/workflows/performance.yml)
workflow runs only when a maintainer is actively reviewing performance. It
uploads a three-day report only on failure or explicit request; the 30-minute
durability profile also requires explicit opt-in. Routine pushes and pull
requests do not consume runner time or artifact storage for performance work.

To reproduce the reviewed profiles or explicitly retain your own JSON, see
[`docs/PERFORMANCE.md`](../PERFORMANCE.md). A changed commit, dirty tree,
different workload is a new measurement. Module implementation performance is
measured separately in `tnt-modules`.
