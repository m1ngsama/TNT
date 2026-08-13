# Reviewed Performance Evidence

Performance reports are code-reviewable evidence, not timeless promises. Each
JSON file records the exact TNT commit, dirty state, host/toolchain, workload,
raw samples, percentile method, correctness checks, and budget outcomes.

Current reviewed baselines:

- [`full-linux-x86_64-1cpu-128m.json`](full-linux-x86_64-1cpu-128m.json) —
  complete 64-session budget profile, including synchronized connection storm,
  all-receiver fan-out, 1,000-message ordered ingest, slow-client pressure,
  capacity rejection, and tnt-modules v0.3.0 enabled/disabled comparison.
- [`soak-linux-x86_64-1cpu-128m.json`](soak-linux-x86_64-1cpu-128m.json) — 64
  fully joined functional sessions held for 30 minutes, with rotating senders,
  all-peer delivery, ordered persistence, survival, and memory samples.

These Linux reports constrain TNT and module children as one cgroup with one
allowed CPU, 128 MiB memory, and no swap. The wrapper is recorded in each
report; OpenSSH load generators remain outside that constraint. The weekly/manual
[`Performance charter`](../../.github/workflows/performance.yml) workflow is the
source of newer Linux evidence and retains both reports for 90 days.

To reproduce the reviewed profiles, see
[`docs/PERFORMANCE.md`](../PERFORMANCE.md). A changed commit, dirty tree,
different module revision, or different workload is a new measurement rather
than an update to an old number.
