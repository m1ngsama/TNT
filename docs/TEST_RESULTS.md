# Continuous Verification Matrix

This document describes how the current tree is verified. It is not a dated
pass certificate: a test result applies only to the commit, platform, toolchain,
configuration, and command recorded by that run. GitHub Actions logs and saved
artifacts are the source of truth for a reviewed commit.

The matrix deliberately does not publish a fixed test count, a blanket security
claim, or performance numbers copied from another machine. Tests are added and
split over time, and benchmark results are meaningful only with their workload
and environment metadata.

## Local Validation Matrix

| Layer | Command | What it verifies | Result contract |
|---|---|---|---|
| Build | `make` | Builds `tnt` and `tntctl` with the default warning and optimization flags | A non-zero exit is a failure |
| Unit | `make unit-test` | UTF-8, input buffering, JSON text, module protocol/runtime, messages, chat-room state, history views, i18n, command/help/manual text, rate limits, defaults, and themes | Every compiled unit binary must exit zero |
| Script contracts | `make script-test` | CLI and documentation surfaces, maintainer/module checks, installer and log tooling, source/release artifact checks, and the performance-driver contract | Every script must exit zero; an explicit dependency skip is not evidence that the skipped behavior passed |
| SSH integration | `make integration-test` | Real SSH health/exec and interactive flows, user lifecycle, join visibility, empty views, module runtime, graceful shutdown, and `tntctl` behavior | Every integration script must exit zero |
| Main suite | `make test` | Build plus unit, script-contract, and SSH integration layers | Strict aggregate gate; integration failures propagate |
| CI-equivalent runtime gate | `make ci-test` | Main suite plus anonymous access, connection limits, and security-feature probes | Strict aggregate gate used by the PR job |
| Release preflight | `make release-check` | Clean rebuild, unit/script layers, version alignment, I/O ownership invariants, staged install layout, log maintenance, and packaging metadata/syntax | Must exit zero before preparing a release |
| Opt-in concurrency | `make stress-test` | Concurrent-client connection and messaging behavior | Configure `CLIENTS` and `DURATION`; command must exit zero |
| Opt-in durability | `make soak-test` | Idle, reconnect, and control-plane durability | Configure `DURATION` and `RECONNECTS`; command must exit zero |
| Opt-in backpressure | `make slow-client-test` | Progress and bounded behavior with an unread interactive client | Configure `DURATION` and `BURST_CHARS`; command must exit zero |
| Target performance | `make perf-full` | 64 joins/storm, all-receiver delivery, ordered ingest, memory, slow-client, and capacity | Every correctness check and performance redline must pass |
| Functional durability | `make perf-soak` | 64 real sessions for 30 minutes with rotating senders, all-peer delivery, ordered persistence, survival, and memory sampling | Every acceptance field in the JSON report must be true |

`test_utf8` is built and run automatically by `make unit-test`; UTF-8 validation
does not require a separate manual compilation step.

## Reproducible Command Sets

Install a C toolchain, GNU Make, libssh development headers, OpenSSH, Python 3.10+,
and `expect`. Then run the strict local suite from the repository root:

```sh
make clean
make test PORT=14200
```

To reproduce the runtime checks used by the cross-platform PR gate, reserve a
free base-port range and run:

```sh
make ci-test CI_TEST_PORT=14200
make release-check
```

The test scripts create temporary state directories and clean them on exit.
Choose another base port when `14200` or the following ports are in use.

The longer release-oriented runtime path is:

```sh
RUN_INTEGRATION=1 RUN_SOAK=1 RUN_SLOW_CLIENT=1 \
  PORT=14200 make release-check
```

Useful focused commands are:

```sh
make unit-test
make script-test
make integration-test PORT=14200
make security-test PORT=14220
CLIENTS=20 DURATION=60 make stress-test PORT=14230
DURATION=1800 RECONNECTS=20 make soak-test PORT=14240
DURATION=30 BURST_CHARS=3200 make slow-client-test PORT=14250
make perf-soak PERF_OUTPUT=/tmp/tnt-perf-soak.json
```

AddressSanitizer and static-analysis entry points are separate from the normal
suite:

```sh
make asan
make check
```

`make asan` proves that an instrumented build succeeds; it is not, by itself,
evidence that every runtime path is memory-safe. Likewise, a ThreadSanitizer
compile probe is not a race-free runtime result. The extended Linux CI job runs
a temporary server under Valgrind and requires an error-free summary.

## Performance Benchmark

The performance driver uses real OpenSSH clients and writes a versioned JSON
report containing workload settings, sample counts, summary statistics, budget
outcomes, Git metadata, and host/toolchain metadata. It requires no third-party
Python packages.

```sh
make perf
make perf PERF_OUTPUT=/tmp/tnt-perf.json
```

The available profiles have different purposes:

| Profile | Command | Purpose |
|---|---|---|
| Benchmark | `make perf` | Normal real-client workload; records measurements without enforcing a budget by default |
| Local smoke gate | `make perf-smoke PERF_OUTPUT=/tmp/tnt-perf-smoke.json` | Short profile that enforces the stable startup, idle RSS, and main-binary redlines while recording broader metrics |
| Normal stable gate | `make perf-check PERF_OUTPUT=/tmp/tnt-perf-check.json` | Normal sample sizes with the same stable redlines enforced |
| Target workload | `make perf-full PERF_OUTPUT=/tmp/tnt-perf-full.json` | Exercises 64 joined sessions and 1,000 ordered messages and gates every eligible redline |
| 30-minute durability | `make perf-soak PERF_OUTPUT=/tmp/tnt-perf-soak.json` | Extends the full 64-session profile to 1,800 seconds and gates correctness, survival, peak RSS, and RSS growth without reconnecting a second client set |

`make script-test` runs `tests/test_perf_benchmark.sh`, which verifies the
driver's percentile/budget helpers, command-line surface, and minimum sample
validation. That contract test does **not** execute the real-client benchmark
and must not be reported as a performance result.

Routine CI intentionally does not run performance workloads. Maintainers can
run the manual Performance Charter workflow while advancing performance work.
It uploads a three-day report only on failure or when explicitly requested. See
[`PERFORMANCE.md`](PERFORMANCE.md) for metric definitions, budgets, workload
controls, percentile rules, and coverage boundaries.

No benchmark values are embedded here. Report measurements only from the JSON
produced by the exact command being discussed, and keep its environment and
workload sections with the result. Do not compare runs from different machines
as though they were a single baseline.

## Continuous Integration Matrix

| Trigger | Jobs and platforms | Required evidence |
|---|---|---|
| Pull request to `main` or `release/**` | Ubuntu release/runtime gate and macOS runtime gate | Default and ASan builds succeed on both; runtime tests pass on both; Ubuntu alone runs the release/package preflight |
| Push to `main` or `release/**` | PR gate plus focused Linux soak/slow-client/Valgrind and portable containers | Extended runtime succeeds; Debian stable and Alpine builds succeed without duplicating native Ubuntu or package jobs |
| Manual performance dispatch | 64-session core profile; durability only when selected | Job log; three-day JSON only when failed or explicitly retained |
| SemVer release tag | Release artifact workflow | Version/tag alignment, architecture-specific builds, source-archive validation, asset collection, and checksum verification; release remains a draft for manual review |

The full workflow and release policy are documented in [`CICD.md`](CICD.md).

## Interpreting and Recording Results

- Record the Git SHA and whether the tree was dirty.
- Record the exact command, environment overrides, OS/architecture, compiler,
  libssh, OpenSSH, and relevant test dependencies.
- Treat a skipped test as `SKIP`, not `PASS`, and state the missing dependency.
- Keep failure output and the first failing command; do not replace it with an
  aggregate success percentage.
- For reviewed performance runs, keep the compact JSON with the decision; do
  not retain routine successful reports without a reason.
- A green suite supports only the behaviors exercised by that suite. It is not
  a blanket declaration that the project has no security, concurrency, or
  memory-safety defects.

This evidence model keeps results reproducible while allowing the suite and its
coverage to evolve without making this document stale after every new test.
