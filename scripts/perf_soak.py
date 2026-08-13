#!/usr/bin/env python3
"""Long-running functional durability benchmark for TNT.

The default profile keeps 64 real OpenSSH TUI sessions joined for 30 minutes.
Every session becomes a sender, every emitted marker must persist in order, and
every joined receiver must render every marker.  Server RSS and virtual memory
are sampled throughout and retained in a machine-readable JSON report.
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time
import uuid
from typing import Any, Sequence

from perf_benchmark import (
    BenchmarkError,
    InteractiveClient,
    configure_server_wrapper,
    environment_metadata,
    git_metadata,
    matching_persisted_messages,
    process_resources,
    ssh_exec,
    start_server,
    summarize,
    utc_now,
    wait_for_health,
    write_report,
)


SCHEMA_VERSION = 1
SESSION_RSS_REDLINE_KIB = 112 * 1024
RSS_GROWTH_REDLINE_KIB = 16 * 1024


def default_output(repo_root: pathlib.Path) -> pathlib.Path:
    stamp = time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
    try:
        sha = subprocess.run(
            ["git", "rev-parse", "--short=12", "HEAD"],
            cwd=repo_root,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
        ).stdout.strip()
    except (FileNotFoundError, subprocess.CalledProcessError):
        sha = "unknown"
    return repo_root / "perf-results" / f"{sha}-{stamp}-soak.json"


def expected_message_count(duration: float, interval: float) -> int:
    if duration <= 0 or interval <= 0:
        return 0
    return 1 + int(max(0.0, duration - 1e-9) // interval)


def validate_args(args: argparse.Namespace) -> None:
    if args.clients < 2:
        raise BenchmarkError("--clients must be at least 2 for peer fan-out")
    for name in ("duration", "message_interval", "sample_interval", "warmup"):
        if getattr(args, name) <= 0:
            raise BenchmarkError(f"--{name.replace('_', '-')} must be positive")
    if args.progress_interval < 0:
        raise BenchmarkError("--progress-interval cannot be negative")
    if expected_message_count(args.duration, args.message_interval) < args.clients:
        raise BenchmarkError(
            "duration/message interval must emit at least one message per client"
        )


def resource_sample(
    pid: int, started: float, clients: Sequence[InteractiveClient]
) -> dict[str, Any]:
    return {
        "elapsed_seconds": round(max(0.0, time.monotonic() - started), 3),
        "sessions_alive": sum(client.process.poll() is None for client in clients),
        **process_resources(pid),
    }


def run_soak(args: argparse.Namespace) -> dict[str, Any]:
    repo_root = pathlib.Path(__file__).resolve().parent.parent
    binary = pathlib.Path(args.binary)
    if not binary.is_absolute():
        binary = (repo_root / binary).resolve()
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise BenchmarkError(f"TNT binary is not executable: {binary}")
    if not shutil.which("ssh"):
        raise BenchmarkError("OpenSSH client (ssh) is required")
    server_wrapper = configure_server_wrapper(args.server_wrapper)

    run_id = uuid.uuid4().hex[:10]
    with tempfile.TemporaryDirectory(prefix="tnt-perf-soak-") as temp:
        state_dir = pathlib.Path(temp)
        server = start_server(
            binary,
            state_dir,
            max_connections=args.clients + 16,
        )
        clients: list[InteractiveClient] = []
        try:
            wait_for_health(server.port)
            for index in range(args.clients):
                clients.append(
                    InteractiveClient(server.port, f"soak-{run_id}-{index:03d}")
                )

            users_result = ssh_exec(server.port, ["users", "--json"], timeout=15)
            if users_result.returncode != 0:
                raise BenchmarkError("users --json failed after durability joins")
            users = set(json.loads(users_result.stdout))
            expected_users = {client.name for client in clients}
            joined = len(expected_users.intersection(users))
            if joined != args.clients:
                raise BenchmarkError(
                    f"durability join verification failed: {joined}/{args.clients}"
                )

            warmup_deadline = time.monotonic() + args.warmup
            while time.monotonic() < warmup_deadline:
                for client in clients:
                    client.drain()
                time.sleep(min(0.1, max(0.0, warmup_deadline - time.monotonic())))

            started = time.monotonic()
            deadline = started + args.duration
            next_message = started
            next_sample = started
            next_progress = started + args.progress_interval
            messages: list[str] = []
            senders: set[str] = set()
            fanout_latencies_ms: list[float] = []
            deliveries_verified = 0
            resources: list[dict[str, Any]] = []

            while time.monotonic() < deadline:
                now = time.monotonic()
                if server.process.poll() is not None:
                    raise BenchmarkError("server exited during durability run")
                dead_clients = [
                    client.name for client in clients if client.process.poll() is not None
                ]
                if dead_clients:
                    raise BenchmarkError(
                        f"interactive sessions exited during durability run: {dead_clients}"
                    )

                if now >= next_message:
                    index = len(messages)
                    sender = clients[index % len(clients)]
                    marker_text = f"perf-soak-{run_id}-{index:06d}"
                    marker = marker_text.encode("ascii")
                    for client in clients:
                        client.drain()
                    message_started_ns = time.monotonic_ns()
                    sender.send(marker + b"\r")
                    fanout_deadline = time.monotonic() + 5
                    receivers = [client for client in clients if client is not sender]
                    for client in receivers:
                        if not client.wait_for(
                            marker,
                            max(0.0, fanout_deadline - time.monotonic()),
                        ):
                            raise BenchmarkError(
                                f"{client.name} did not render durability marker {marker_text}"
                            )
                        deliveries_verified += 1
                    fanout_latencies_ms.append(
                        (time.monotonic_ns() - message_started_ns) / 1_000_000
                    )
                    messages.append(marker_text)
                    senders.add(sender.name)
                    next_message += args.message_interval

                if now >= next_sample:
                    resources.append(resource_sample(server.process.pid, started, clients))
                    next_sample += args.sample_interval

                if args.progress_interval > 0 and now >= next_progress:
                    latest_rss = resources[-1]["rss_kib"] if resources else None
                    print(
                        "durability progress: "
                        f"elapsed={now - started:.0f}s messages={len(messages)} "
                        f"rss_kib={latest_rss}",
                        file=sys.stderr,
                        flush=True,
                    )
                    next_progress += args.progress_interval

                for client in clients:
                    client.drain()
                now = time.monotonic()
                sleep_until = min(deadline, next_message, next_sample)
                if args.progress_interval > 0:
                    sleep_until = min(sleep_until, next_progress)
                time.sleep(min(0.25, max(0.0, sleep_until - now)))

            elapsed = time.monotonic() - started
            resources.append(resource_sample(server.process.pid, started, clients))
            persisted = matching_persisted_messages(
                state_dir / "messages.log", f"perf-soak-{run_id}-"
            )
            final_users_result = ssh_exec(server.port, ["users", "--json"], timeout=15)
            final_users = (
                set(json.loads(final_users_result.stdout))
                if final_users_result.returncode == 0
                else set()
            )
            health_result = ssh_exec(server.port, ["health"], timeout=15)

            rss_values = [
                sample["rss_kib"]
                for sample in resources
                if sample["rss_kib"] is not None
            ]
            virtual_values = [
                sample["virtual_kib"]
                for sample in resources
                if sample["virtual_kib"] is not None
            ]
            initial_rss = rss_values[0] if rss_values else None
            final_rss = rss_values[-1] if rss_values else None
            rss_growth = (
                final_rss - initial_rss
                if initial_rss is not None and final_rss is not None
                else None
            )
            acceptance = {
                "duration_reached": elapsed >= args.duration,
                "all_sessions_joined": joined == args.clients,
                "every_session_sent": len(senders) == args.clients,
                "messages_complete": persisted == messages,
                "messages_ordered": persisted == messages,
                "fanout_complete": deliveries_verified
                == len(messages) * (args.clients - 1),
                "all_sessions_survived": len(expected_users.intersection(final_users))
                == args.clients,
                "server_healthy": (
                    health_result.returncode == 0
                    and health_result.stdout.strip() == b"ok"
                ),
                "peak_rss_within_112_mib": bool(rss_values)
                and max(rss_values) <= SESSION_RSS_REDLINE_KIB,
                "rss_growth_within_16_mib": rss_growth is not None
                and rss_growth <= RSS_GROWTH_REDLINE_KIB,
            }
            return {
                "schema_version": SCHEMA_VERSION,
                "kind": "functional_durability",
                "generated_at": utc_now(),
                "status": "ok" if all(acceptance.values()) else "failed",
                "commit": git_metadata(repo_root),
                "environment": environment_metadata(binary),
                "configuration": {
                    "clients": args.clients,
                    "duration_seconds": args.duration,
                    "warmup_seconds": args.warmup,
                    "message_interval_seconds": args.message_interval,
                    "resource_sample_interval_seconds": args.sample_interval,
                    "server_wrapper": args.server_wrapper,
                    "server_wrapper_argv": server_wrapper,
                },
                "metrics": {
                    "duration_seconds": round(elapsed, 3),
                    "joined_sessions": joined,
                    "sessions_survived": len(expected_users.intersection(final_users)),
                    "unique_senders": len(senders),
                    "messages_sent": len(messages),
                    "messages_persisted": len(persisted),
                    "deliveries_verified": deliveries_verified,
                    "fanout_all_receivers_ms": summarize(fanout_latencies_ms),
                    "resources": resources,
                    "initial_rss_kib": initial_rss,
                    "final_rss_kib": final_rss,
                    "peak_rss_kib": max(rss_values) if rss_values else None,
                    "rss_growth_kib": rss_growth,
                    "peak_virtual_kib": max(virtual_values) if virtual_values else None,
                },
                "acceptance": acceptance,
                "measurement_contract": {
                    "session": (
                        "real OpenSSH TUI session confirmed after room add and in users --json"
                    ),
                    "message": (
                        "rotating interactive sender, ordered persistence, and marker "
                        "rendered by every other joined session; sender echo is excluded"
                    ),
                    "memory": (
                        "server-process RSS and virtual memory sampled after join warmup; "
                        "OpenSSH load generators are excluded"
                    ),
                    "server_wrapper": (
                        "optional exec prefix constrains TNT and is recorded in configuration"
                    ),
                },
            }
        finally:
            for client in reversed(clients):
                client.close()
            server.stop()


def failure_report(
    args: argparse.Namespace,
    repo_root: pathlib.Path,
    error: BaseException,
) -> dict[str, Any]:
    binary = pathlib.Path(args.binary)
    if not binary.is_absolute():
        binary = (repo_root / binary).resolve()
    return {
        "schema_version": SCHEMA_VERSION,
        "kind": "functional_durability",
        "generated_at": utc_now(),
        "status": "error",
        "error": {"type": type(error).__name__, "message": str(error)},
        "commit": git_metadata(repo_root),
        "environment": environment_metadata(binary),
        "configuration": {
            "clients": args.clients,
            "duration_seconds": args.duration,
            "warmup_seconds": args.warmup,
            "message_interval_seconds": args.message_interval,
            "resource_sample_interval_seconds": args.sample_interval,
            "server_wrapper": args.server_wrapper,
            "server_wrapper_argv": None,
        },
        "metrics_complete": False,
    }


def self_test() -> int:
    assert expected_message_count(1800, 10) == 180
    assert expected_message_count(1, 0.2) == 5
    assert expected_message_count(0, 1) == 0
    print("perf_soak self-test passed")
    return 0


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run TNT's 64-session functional durability benchmark."
    )
    parser.add_argument("--binary", default="./tnt")
    parser.add_argument("--output")
    parser.add_argument("--clients", type=int, default=64)
    parser.add_argument("--duration", type=float, default=1800.0)
    parser.add_argument("--warmup", type=float, default=1.0)
    parser.add_argument("--message-interval", type=float, default=10.0)
    parser.add_argument("--sample-interval", type=float, default=10.0)
    parser.add_argument("--progress-interval", type=float, default=60.0)
    parser.add_argument("--server-wrapper")
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    if args.self_test:
        return self_test()

    repo_root = pathlib.Path(__file__).resolve().parent.parent
    output: pathlib.Path | None = None
    try:
        validate_args(args)
        output = pathlib.Path(args.output) if args.output else default_output(repo_root)
        if not output.is_absolute():
            output = (repo_root / output).resolve()
        report = run_soak(args)
        write_report(output, report)
        json.dump(report, sys.stdout, ensure_ascii=False, sort_keys=True)
        sys.stdout.write("\n")
        print(f"durability report: {output}", file=sys.stderr)
        return 0 if report["status"] == "ok" else 1
    except (BenchmarkError, OSError, subprocess.SubprocessError, json.JSONDecodeError) as error:
        if output is not None:
            try:
                write_report(output, failure_report(args, repo_root, error))
                print(f"durability diagnostic report: {output}", file=sys.stderr)
            except (OSError, subprocess.SubprocessError) as report_error:
                print(
                    f"perf_soak: could not write diagnostic report: {report_error}",
                    file=sys.stderr,
                )
        print(f"perf_soak: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
