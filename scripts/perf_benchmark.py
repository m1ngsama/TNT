#!/usr/bin/env python3
"""Reproducible, dependency-free performance benchmark for TNT.

The benchmark deliberately uses real OpenSSH clients.  Interactive clients do
not count as joined until TNT enables bracketed paste mode after room_add_client
has succeeded.  Results are written as JSON so runs can be compared without
scraping human-oriented output.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import ctypes
import ctypes.util
import datetime as dt
import json
import math
import os
import pathlib
import platform
import re
import selectors
import shlex
import shutil
import signal
import socket
import statistics
import subprocess
import sys
import tempfile
import threading
import time
import uuid
from dataclasses import dataclass
from typing import Any, Sequence


SCHEMA_VERSION = 3
JOIN_MARKER = b"\x1b[?2004h"
MIB = 1024 * 1024

BUDGETS = {
    "existing_key_startup_p95_ms": {"ideal": 20.0, "redline": 50.0},
    "idle_rss_kib": {"ideal": 8 * 1024, "redline": 16 * 1024},
    "sessions_64_rss_kib": {"ideal": 80 * 1024, "redline": 112 * 1024},
    "ssh_handshake_p95_ms": {"ideal": 50.0, "redline": 100.0},
    "persisted_to_all_receivers_p99_ms": {
        "ideal": 5.0,
        "redline": 20.0,
    },
    "message_ingest_per_second": {"ideal": 1000.0, "redline": 500.0},
    "main_binary_bytes": {"ideal": 256 * 1024, "redline": 512 * 1024},
}

STABLE_GATE_METRICS = {
    "existing_key_startup_p95_ms",
    "idle_rss_kib",
    "main_binary_bytes",
}

_SERVER_WRAPPER: list[str] = []


class BenchmarkError(RuntimeError):
    """Raised when a benchmark scenario cannot produce a valid measurement."""


def configure_server_wrapper(value: str | None) -> list[str]:
    """Set an explicit, reportable exec prefix for target-host constraints."""
    global _SERVER_WRAPPER
    if not value:
        _SERVER_WRAPPER = []
        return []
    try:
        parsed = shlex.split(value)
    except ValueError as error:
        raise BenchmarkError(f"invalid --server-wrapper: {error}") from error
    if not parsed:
        raise BenchmarkError("--server-wrapper must contain a command")
    if not shutil.which(parsed[0]):
        raise BenchmarkError(f"server wrapper executable not found: {parsed[0]}")
    _SERVER_WRAPPER = parsed
    return list(parsed)


def utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace(
        "+00:00", "Z"
    )


def percentile(values: Sequence[float], fraction: float) -> float:
    """Return a nearest-rank percentile (the documented report convention)."""
    if not values:
        raise ValueError("cannot calculate a percentile of an empty sample")
    ordered = sorted(float(value) for value in values)
    rank = max(1, math.ceil(fraction * len(ordered)))
    return ordered[rank - 1]


def summarize(values: Sequence[float]) -> dict[str, Any]:
    if not values:
        raise ValueError("cannot summarize an empty sample")
    samples = [round(float(value), 3) for value in values]
    return {
        "samples": samples,
        "sample_count": len(samples),
        "min": round(min(samples), 3),
        "max": round(max(samples), 3),
        "mean": round(statistics.fmean(samples), 3),
        "p50": round(percentile(samples, 0.50), 3),
        "p95": round(percentile(samples, 0.95), 3),
        "p99": round(percentile(samples, 0.99), 3),
    }


def run_text(
    command: Sequence[str], *, cwd: pathlib.Path | None = None, check: bool = False
) -> str:
    completed = subprocess.run(
        command,
        cwd=cwd,
        check=check,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
    )
    return completed.stdout.strip()


def first_line(command: Sequence[str]) -> str:
    try:
        return run_text(command).splitlines()[0]
    except (FileNotFoundError, IndexError):
        return "unknown"


def version_line(command: Sequence[str]) -> str:
    try:
        completed = subprocess.run(
            command,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        return completed.stdout.splitlines()[0] if completed.stdout else "unknown"
    except FileNotFoundError:
        return "unknown"


def git_metadata(repo_root: pathlib.Path) -> dict[str, Any]:
    try:
        sha = run_text(["git", "rev-parse", "HEAD"], cwd=repo_root, check=True)
        dirty = bool(run_text(["git", "status", "--porcelain"], cwd=repo_root))
        branch = run_text(["git", "branch", "--show-current"], cwd=repo_root)
    except (FileNotFoundError, subprocess.CalledProcessError):
        sha = "unknown"
        dirty = True
        branch = "unknown"
    return {"sha": sha, "branch": branch or "detached", "dirty": dirty}


def cpu_model() -> str:
    if sys.platform.startswith("linux"):
        try:
            for line in pathlib.Path("/proc/cpuinfo").read_text(
                encoding="utf-8", errors="replace"
            ).splitlines():
                if line.startswith("model name"):
                    return line.split(":", 1)[1].strip()
        except OSError:
            pass
    if sys.platform == "darwin":
        model = run_text(["sysctl", "-n", "machdep.cpu.brand_string"])
        if model:
            return model
        model = run_text(["sysctl", "-n", "hw.model"])
        if model:
            return model
    return platform.processor() or "unknown"


def total_memory_kib() -> int | None:
    if sys.platform.startswith("linux"):
        try:
            for line in pathlib.Path("/proc/meminfo").read_text().splitlines():
                if line.startswith("MemTotal:"):
                    return int(line.split()[1])
        except (OSError, ValueError, IndexError):
            pass
    if sys.platform == "darwin":
        try:
            return int(run_text(["sysctl", "-n", "hw.memsize"])) // 1024
        except (ValueError, FileNotFoundError):
            pass
    return None


def environment_metadata(binary: pathlib.Path) -> dict[str, Any]:
    libssh_version = "unknown"
    if shutil.which("pkg-config"):
        libssh_version = run_text(["pkg-config", "--modversion", "libssh"]) or "unknown"
    if libssh_version == "unknown":
        try:
            library_path = ctypes.util.find_library("ssh")
            if library_path:
                library = ctypes.CDLL(library_path)
                library.ssh_version.argtypes = [ctypes.c_int]
                library.ssh_version.restype = ctypes.c_char_p
                runtime_version = library.ssh_version(0)
                if runtime_version:
                    libssh_version = runtime_version.decode("ascii", errors="replace")
        except (AttributeError, OSError):
            pass
    return {
        "os": platform.platform(),
        "kernel": platform.release(),
        "architecture": platform.machine(),
        "cpu_model": cpu_model(),
        "logical_cpus": os.cpu_count(),
        "driver_cpu_affinity": (
            sorted(os.sched_getaffinity(0))
            if hasattr(os, "sched_getaffinity")
            else None
        ),
        "total_memory_kib": total_memory_kib(),
        "python": platform.python_version(),
        "ssh": version_line(["ssh", "-V"]),
        "compiler": first_line([os.environ.get("CC", "cc"), "--version"]),
        "libssh": libssh_version,
        "binary": str(binary),
    }


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def wait_readable(stream: Any, timeout: float) -> bytes:
    selector = selectors.DefaultSelector()
    selector.register(stream, selectors.EVENT_READ)
    try:
        events = selector.select(timeout)
        if not events:
            return b""
        try:
            return os.read(stream.fileno(), 65536)
        except BlockingIOError:
            return b""
    finally:
        selector.close()


@dataclass
class ServerProcess:
    process: subprocess.Popen[bytes]
    port: int
    state_dir: pathlib.Path
    startup_ms: float
    stderr_path: pathlib.Path

    def stop(self) -> None:
        if self.process.poll() is None:
            self.process.send_signal(signal.SIGTERM)
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        if self.process.stdout is not None and not self.process.stdout.closed:
            self.process.stdout.close()


def _start_server_once(
    binary: pathlib.Path,
    state_dir: pathlib.Path,
    *,
    max_connections: int = 1024,
    module_paths: str | None = None,
    port: int,
    timeout: float = 15.0,
) -> ServerProcess:
    selected_port = port
    stderr_path = state_dir / f"server-{selected_port}.stderr.log"
    stderr_file = stderr_path.open("ab", buffering=0)
    environment = os.environ.copy()
    # A benchmark must not inherit optional production behavior.  In
    # particular, modules would change both the workload and process tree, and
    # an access token would make the anonymous real-client fixture fail.
    for name in (
        "PORT",
        "TNT_ACCESS_TOKEN",
        "TNT_BIND_ADDR",
        "TNT_PUBLIC_HOST",
        "TNT_STATE_DIR",
        "TNT_MODULE_PATHS",
    ):
        environment.pop(name, None)
    environment.update(
        {
            "TNT_LANG": "en",
            "TNT_RATE_LIMIT": "0",
            "TNT_MAX_CONNECTIONS": str(max_connections),
            "TNT_MAX_CONN_PER_IP": str(max_connections),
            "TNT_MAX_CONN_RATE_PER_IP": str(max_connections),
            "TNT_IDLE_TIMEOUT": "0",
            "TNT_SSH_LOG_LEVEL": "0",
        }
    )
    if module_paths:
        environment["TNT_MODULE_PATHS"] = module_paths
    command = [
        *_SERVER_WRAPPER,
        str(binary),
        "--bind",
        "127.0.0.1",
        "--port",
        str(selected_port),
        "--state-dir",
        str(state_dir),
    ]
    started_ns = time.monotonic_ns()
    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=stderr_file,
        env=environment,
        bufsize=0,
    )
    stderr_file.close()
    if process.stdout is None:
        process.kill()
        raise BenchmarkError("server stdout pipe was not created")

    deadline = time.monotonic() + timeout
    output = bytearray()
    while time.monotonic() < deadline:
        if process.poll() is not None:
            detail = stderr_path.read_text(encoding="utf-8", errors="replace")
            process.stdout.close()
            raise BenchmarkError(
                f"server exited before readiness (status {process.returncode}): {detail}"
            )
        chunk = wait_readable(process.stdout, max(0.0, deadline - time.monotonic()))
        if chunk:
            output.extend(chunk)
            if b"TNT chat server listening on port" in output:
                elapsed_ms = (time.monotonic_ns() - started_ns) / 1_000_000
                return ServerProcess(
                    process=process,
                    port=selected_port,
                    state_dir=state_dir,
                    startup_ms=elapsed_ms,
                    stderr_path=stderr_path,
                )
        elif process.poll() is not None:
            continue

    process.kill()
    process.wait(timeout=5)
    process.stdout.close()
    detail = stderr_path.read_text(encoding="utf-8", errors="replace")
    raise BenchmarkError(f"server readiness timed out: {detail}")


def start_server(
    binary: pathlib.Path,
    state_dir: pathlib.Path,
    *,
    max_connections: int = 1024,
    module_paths: str | None = None,
    port: int | None = None,
    timeout: float = 15.0,
) -> ServerProcess:
    """Start TNT, retrying the ephemeral-port handoff if another process wins."""
    attempts = 1 if port is not None else 5
    last_error: BenchmarkError | None = None
    for _ in range(attempts):
        selected_port = port if port is not None else free_port()
        try:
            return _start_server_once(
                binary,
                state_dir,
                max_connections=max_connections,
                module_paths=module_paths,
                port=selected_port,
                timeout=timeout,
            )
        except BenchmarkError as error:
            last_error = error
            if port is not None or "Failed to bind to port" not in str(error):
                raise
    assert last_error is not None
    raise last_error


def ssh_options(port: int) -> list[str]:
    return [
        "-o",
        "BatchMode=yes",
        "-o",
        "PubkeyAuthentication=no",
        "-o",
        "PasswordAuthentication=no",
        "-o",
        "KbdInteractiveAuthentication=no",
        "-o",
        "PreferredAuthentications=none",
        "-o",
        "NumberOfPasswordPrompts=0",
        "-o",
        "StrictHostKeyChecking=no",
        "-o",
        "UserKnownHostsFile=/dev/null",
        "-o",
        "LogLevel=ERROR",
        "-o",
        "ControlMaster=no",
        "-o",
        "ConnectTimeout=5",
        "-p",
        str(port),
    ]


def ssh_exec(
    port: int,
    command: Sequence[str],
    *,
    username: str = "benchmark",
    timeout: float = 10.0,
) -> subprocess.CompletedProcess[bytes]:
    return subprocess.run(
        ["ssh", "-n", *ssh_options(port), f"{username}@127.0.0.1", *command],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout,
    )


def wait_for_health(port: int, timeout: float = 10.0) -> None:
    deadline = time.monotonic() + timeout
    last_error = ""
    while time.monotonic() < deadline:
        try:
            result = ssh_exec(port, ["health"], timeout=5)
            if result.returncode == 0 and result.stdout.strip() == b"ok":
                return
            last_error = result.stderr.decode("utf-8", errors="replace")
        except subprocess.TimeoutExpired:
            last_error = "SSH health command timed out"
        time.sleep(0.02)
    raise BenchmarkError(f"server did not answer health check: {last_error}")


class InteractiveClient:
    def __init__(
        self,
        port: int,
        name: str,
        timeout: float = 15.0,
        receive_buffer_bytes: int | None = None,
    ) -> None:
        self.name = name
        # One full-duplex socket replaces separate stdin/stdout pipes. This
        # keeps the 64-client target below macOS's common 256-FD soft limit and
        # still gives each OpenSSH process independent byte streams.
        parent_io, child_io = socket.socketpair()
        if receive_buffer_bytes is not None:
            parent_io.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, receive_buffer_bytes)
            child_io.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, receive_buffer_bytes)
        try:
            self.process = subprocess.Popen(
                [
                    "ssh",
                    "-e",
                    "none",
                    "-tt",
                    *ssh_options(port),
                    f"{name}@127.0.0.1",
                ],
                stdin=child_io,
                stdout=child_io,
                stderr=subprocess.DEVNULL,
                bufsize=0,
            )
        except BaseException:
            parent_io.close()
            child_io.close()
            raise
        child_io.close()
        self._io = parent_io
        self._io.setblocking(False)
        self._buffer = bytearray()
        self.send(name.encode("utf-8") + b"\r")
        if not self.wait_for(JOIN_MARKER, timeout):
            status = self.process.poll()
            self.close()
            raise BenchmarkError(
                f"interactive client {name!r} did not complete room join (status={status})"
            )

    def _read(self, timeout: float) -> bytes:
        if not hasattr(self, "_io") or self._io.fileno() < 0:
            return b""
        selector = selectors.DefaultSelector()
        selector.register(self._io, selectors.EVENT_READ)
        try:
            events = selector.select(timeout)
            if not events:
                return b""
            chunks = bytearray()
            while True:
                try:
                    chunk = self._io.recv(65536)
                except BlockingIOError:
                    break
                if not chunk:
                    break
                chunks.extend(chunk)
                if len(chunk) < 65536:
                    break
            return bytes(chunks)
        finally:
            selector.close()

    def drain(self) -> bytes:
        output = bytearray(self._buffer)
        self._buffer.clear()
        while True:
            chunk = self._read(0)
            if not chunk:
                break
            output.extend(chunk)
        return bytes(output)

    def wait_for(self, marker: bytes, timeout: float) -> bool:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if marker in self._buffer:
                self._buffer.clear()
                return True
            if self.process.poll() is not None:
                return False
            chunk = self._read(max(0.0, deadline - time.monotonic()))
            if chunk:
                self._buffer.extend(chunk)
                if len(self._buffer) > 2 * MIB:
                    del self._buffer[:-MIB]
        return marker in self._buffer

    def send(self, data: bytes) -> None:
        if (
            not hasattr(self, "_io")
            or self._io.fileno() < 0
            or self.process.poll() is not None
        ):
            raise BenchmarkError(f"interactive client {self.name!r} is closed")
        view = memoryview(data)
        while view:
            try:
                sent = self._io.send(view)
            except BlockingIOError:
                selector = selectors.DefaultSelector()
                selector.register(self._io, selectors.EVENT_WRITE)
                try:
                    if not selector.select(5):
                        raise BenchmarkError(
                            f"interactive client {self.name!r} write timed out"
                        )
                finally:
                    selector.close()
                continue
            if sent <= 0:
                raise BenchmarkError(f"interactive client {self.name!r} write failed")
            view = view[sent:]

    def close(self) -> None:
        if not hasattr(self, "process"):
            return
        if self.process.poll() is None:
            try:
                self.send(b"\x03\x03")
                self.process.wait(timeout=2)
            except (BenchmarkError, BrokenPipeError, subprocess.TimeoutExpired):
                self.process.terminate()
                try:
                    self.process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait(timeout=2)
        if hasattr(self, "_io"):
            self._io.close()


def process_resources(pid: int) -> dict[str, int | None]:
    resources: dict[str, int | None] = {
        "rss_kib": None,
        "virtual_kib": None,
        "threads": None,
    }
    status_path = pathlib.Path(f"/proc/{pid}/status")
    if status_path.exists():
        try:
            for line in status_path.read_text().splitlines():
                if line.startswith("VmRSS:"):
                    resources["rss_kib"] = int(line.split()[1])
                elif line.startswith("VmSize:"):
                    resources["virtual_kib"] = int(line.split()[1])
                elif line.startswith("Threads:"):
                    resources["threads"] = int(line.split()[1])
            return resources
        except (OSError, ValueError, IndexError):
            pass

    try:
        output = run_text(["ps", "-o", "rss=,vsz=,thcount=", "-p", str(pid)])
        fields = output.split()
        if len(fields) >= 2:
            resources["rss_kib"] = int(fields[0])
            resources["virtual_kib"] = int(fields[1])
        if len(fields) >= 3:
            resources["threads"] = int(fields[2])
    except (FileNotFoundError, ValueError):
        pass
    return resources


def process_constraints(pid: int) -> dict[str, Any]:
    """Report effective Linux affinity and cgroup-v2 memory constraints."""
    constraints: dict[str, Any] = {
        "cpu_allowed_list": None,
        "cgroup_v2_path": None,
        "cpuset_cpus_effective": None,
        "memory_max_bytes": None,
        "memory_swap_max_bytes": None,
    }
    try:
        for line in pathlib.Path(f"/proc/{pid}/status").read_text().splitlines():
            if line.startswith("Cpus_allowed_list:"):
                constraints["cpu_allowed_list"] = line.split(":", 1)[1].strip()
                break
    except OSError:
        pass

    try:
        cgroup_lines = pathlib.Path(f"/proc/{pid}/cgroup").read_text().splitlines()
        relative = next(
            line.split("::", 1)[1] for line in cgroup_lines if line.startswith("0::")
        )
        constraints["cgroup_v2_path"] = relative
        cgroup_dir = pathlib.Path("/sys/fs/cgroup") / relative.lstrip("/")
        files = {
            "cpuset_cpus_effective": "cpuset.cpus.effective",
            "memory_max_bytes": "memory.max",
            "memory_swap_max_bytes": "memory.swap.max",
        }
        for field, filename in files.items():
            try:
                value = (cgroup_dir / filename).read_text().strip()
            except OSError:
                continue
            if field.startswith("memory_") and value != "max":
                try:
                    constraints[field] = int(value)
                    continue
                except ValueError:
                    pass
            constraints[field] = value
    except (OSError, StopIteration, IndexError):
        pass
    return constraints


def require_process_constraints(
    constraints: dict[str, Any],
    *,
    cpu_list: str | None,
    memory_max_bytes: int | None,
    memory_swap_max_bytes: int | None,
) -> None:
    """Fail when an explicitly requested target constraint was not applied."""
    expected = {
        "cpu_allowed_list": cpu_list,
        "memory_max_bytes": memory_max_bytes,
        "memory_swap_max_bytes": memory_swap_max_bytes,
    }
    for field, wanted in expected.items():
        if wanted is not None and constraints.get(field) != wanted:
            raise BenchmarkError(
                f"server constraint mismatch for {field}: "
                f"expected {wanted!r}, observed {constraints.get(field)!r}"
            )


def direct_child_pids(pid: int) -> list[int]:
    """Return direct child PIDs without relying on platform-specific pgrep flags."""
    try:
        output = run_text(["ps", "-axo", "pid=,ppid="])
    except FileNotFoundError:
        return []
    children: list[int] = []
    for line in output.splitlines():
        fields = line.split()
        if len(fields) != 2:
            continue
        try:
            child_pid, parent_pid = (int(field) for field in fields)
        except ValueError:
            continue
        if parent_pid == pid:
            children.append(child_pid)
    return sorted(children)


def process_tree_resources(pid: int) -> dict[str, Any]:
    pids = [pid, *direct_child_pids(pid)]
    processes = [
        {"pid": process_pid, **process_resources(process_pid)} for process_pid in pids
    ]
    rss_values = [
        process["rss_kib"]
        for process in processes
        if process["rss_kib"] is not None
    ]
    virtual_values = [
        process["virtual_kib"]
        for process in processes
        if process["virtual_kib"] is not None
    ]
    return {
        "processes": processes,
        "process_count": len(processes),
        "rss_kib": sum(rss_values) if len(rss_values) == len(processes) else None,
        "virtual_kib": (
            sum(virtual_values) if len(virtual_values) == len(processes) else None
        ),
    }


def process_cpu_seconds(pid: int) -> float | None:
    stat_path = pathlib.Path(f"/proc/{pid}/stat")
    if stat_path.exists():
        try:
            fields = stat_path.read_text().split()
            ticks = int(fields[13]) + int(fields[14])
            return ticks / os.sysconf("SC_CLK_TCK")
        except (OSError, ValueError, IndexError):
            pass
    try:
        text = run_text(["ps", "-o", "time=", "-p", str(pid)])
        match = re.fullmatch(r"\s*(?:(\d+)-)?(\d+):(\d+)(?:\.(\d+))?\s*", text)
        if not match:
            return None
        days, minutes, seconds, fraction = match.groups()
        value = int(days or 0) * 86400 + int(minutes) * 60 + int(seconds)
        if fraction:
            value += int(fraction) / (10 ** len(fraction))
        return float(value)
    except (FileNotFoundError, ValueError):
        return None


def measure_idle_cpu(
    server: ServerProcess, clients: Sequence[InteractiveClient], seconds: float
) -> dict[str, Any]:
    for client in clients:
        client.drain()
    before = process_cpu_seconds(server.process.pid)
    started = time.monotonic()
    deadline = started + seconds
    while time.monotonic() < deadline:
        for client in clients:
            client.drain()
        time.sleep(min(0.05, max(0.0, deadline - time.monotonic())))
    elapsed = time.monotonic() - started
    after = process_cpu_seconds(server.process.pid)
    cpu_percent = None
    if before is not None and after is not None and elapsed > 0:
        cpu_percent = max(0.0, (after - before) * 100.0 / elapsed)
    return {
        "duration_seconds": round(elapsed, 3),
        "process_cpu_seconds": (
            round(max(0.0, after - before), 6)
            if before is not None and after is not None
            else None
        ),
        "one_core_cpu_percent": round(cpu_percent, 3) if cpu_percent is not None else None,
    }


def generate_history(path: pathlib.Path, record_count: int) -> None:
    timestamp = "2026-01-01T00:00:00Z"
    with path.open("w", encoding="utf-8", newline="\n") as handle:
        for index in range(record_count):
            handle.write(f"{timestamp}|history|benchmark history {index:06d}\n")


def request_file_cache_drop(path: pathlib.Path) -> bool:
    """Best-effort per-file cache eviction; never mutates global host caches."""
    advise: Any = getattr(os, "posix_fadvise", None)
    dontneed = getattr(os, "POSIX_FADV_DONTNEED", None)
    if not callable(advise) or dontneed is None:
        return False
    fd = os.open(path, os.O_RDONLY)
    try:
        advise(fd, 0, 0, dontneed)  # pylint: disable=not-callable
        return True
    except OSError:
        return False
    finally:
        os.close(fd)


def measure_startups(
    binary: pathlib.Path, state_dir: pathlib.Path, samples: int
) -> tuple[float, list[float]]:
    first = start_server(binary, state_dir)
    first_ms = first.startup_ms
    first.stop()

    measured: list[float] = []
    for _ in range(samples):
        server = start_server(binary, state_dir)
        measured.append(server.startup_ms)
        server.stop()
    return first_ms, measured


def measure_connection_storm(
    binary: pathlib.Path,
    state_dir: pathlib.Path,
    client_count: int,
    run_id: str,
) -> dict[str, Any]:
    """Synchronize real interactive clients and require every room join."""
    server = start_server(
        binary,
        state_dir,
        max_connections=client_count + 16,
    )
    clients: list[InteractiveClient] = []
    errors: list[str] = []
    try:
        wait_for_health(server.port)
        barrier = threading.Barrier(client_count)

        def connect(index: int) -> tuple[int, float, InteractiveClient]:
            name = f"storm-{run_id}-{index:03d}"
            barrier.wait(timeout=30)
            started_ns = time.monotonic_ns()
            client = InteractiveClient(server.port, name, timeout=30)
            elapsed_ms = (time.monotonic_ns() - started_ns) / 1_000_000
            return index, elapsed_ms, client

        results: list[tuple[int, float, InteractiveClient]] = []
        with concurrent.futures.ThreadPoolExecutor(
            max_workers=client_count,
            thread_name_prefix="tnt-perf-storm",
        ) as executor:
            futures = [executor.submit(connect, index) for index in range(client_count)]
            for future in futures:
                try:
                    results.append(future.result())
                except Exception as error:
                    errors.append(f"{type(error).__name__}: {error}")

        results.sort(key=lambda item: item[0])
        clients.extend(item[2] for item in results)
        latencies = [item[1] for item in results]

        users_result = ssh_exec(server.port, ["users", "--json"], timeout=15)
        if users_result.returncode != 0:
            raise BenchmarkError("users --json failed after connection storm")
        users = set(json.loads(users_result.stdout))
        expected = {client.name for client in clients}
        confirmed = expected.intersection(users)
        healthy = ssh_exec(server.port, ["health"], timeout=15).stdout.strip() == b"ok"
        report = {
            "requested": client_count,
            "completed_room_joins": len(clients),
            "confirmed_in_users": len(confirmed),
            "join_latency_ms": summarize(latencies) if latencies else None,
            "errors": errors,
            "server_healthy": healthy,
            "server_resources": process_resources(server.process.pid),
            "synchronized_by": "thread barrier before OpenSSH process start",
            "join_contract": (
                "authentication, username submission, room add, and "
                "post-join bracketed-paste marker"
            ),
        }
        if errors or len(clients) != client_count or len(confirmed) != client_count:
            raise BenchmarkError(
                "connection storm did not complete every room join: "
                f"joined={len(clients)} confirmed={len(confirmed)} "
                f"requested={client_count} errors={errors[:3]}"
            )
        if not healthy:
            raise BenchmarkError("server health failed after connection storm")
        return report
    finally:
        for client in reversed(clients):
            client.close()
        server.stop()


def measure_handshakes(port: int, samples: int) -> list[float]:
    measured: list[float] = []
    for _ in range(samples):
        started_ns = time.monotonic_ns()
        result = ssh_exec(port, ["health"])
        elapsed_ms = (time.monotonic_ns() - started_ns) / 1_000_000
        if result.returncode != 0 or result.stdout.strip() != b"ok":
            raise BenchmarkError(
                "health handshake failed: "
                + result.stderr.decode("utf-8", errors="replace")
            )
        measured.append(elapsed_ms)
    return measured


def measure_exec_post_to_all_receivers(
    port: int,
    receivers: Sequence[InteractiveClient],
    samples: int,
    run_id: str,
    log_path: pathlib.Path,
) -> dict[str, Any]:
    measured: list[float] = []
    for receiver in receivers:
        receiver.drain()
    warmup_marker = f"pfw{run_id[:4]}".encode()
    warmup_result = ssh_exec(
        port, ["post", warmup_marker.decode("ascii")], username="perfbot"
    )
    if warmup_result.returncode != 0 or warmup_result.stdout.strip() != b"posted":
        raise BenchmarkError("fanout warmup post failed")
    warmup_deadline = time.monotonic() + 5
    for receiver in receivers:
        if not receiver.wait_for(
            warmup_marker, max(0.0, warmup_deadline - time.monotonic())
        ):
            raise BenchmarkError(
                f"receiver {receiver.name!r} did not render fanout warmup"
            )
    for index in range(samples):
        marker = f"pf{run_id[:4]}{index:03d}".encode()
        started_ns = time.monotonic_ns()
        result = ssh_exec(
            port, ["post", marker.decode("ascii")], username="perfbot"
        )
        if result.returncode != 0 or result.stdout.strip() != b"posted":
            raise BenchmarkError(
                "fanout post failed: "
                + result.stderr.decode("utf-8", errors="replace")
            )
        deadline = time.monotonic() + 5
        for receiver in receivers:
            if not receiver.wait_for(marker, max(0.0, deadline - time.monotonic())):
                raise BenchmarkError(
                    f"receiver {receiver.name!r} did not render fanout marker {marker!r}"
                )
        persisted = matching_persisted_messages(
            log_path, marker.decode("ascii")
        )
        if persisted != [marker.decode("ascii")]:
            raise BenchmarkError(f"fanout marker was not persisted exactly once: {marker!r}")
        measured.append((time.monotonic_ns() - started_ns) / 1_000_000)
    return {
        **summarize(measured),
        "joined_receivers": len(receivers),
        "messages": samples,
        "warmup_messages": 1,
        "deliveries_verified": len(receivers) * samples,
        "persistence_verified": samples,
        "completion": "every joined receiver rendered the persisted marker",
        "sender_transport": "fresh SSH exec post",
        "conservative_scope": (
            "includes OpenSSH process start, authentication, persistence, room "
            "fan-out, wakeup, render, and transport to every joined receiver"
        ),
    }


def measure_interactive_distribution(
    sender: InteractiveClient,
    receivers: Sequence[InteractiveClient],
    samples: int,
    run_id: str,
    log_path: pathlib.Path,
) -> dict[str, Any]:
    """Measure server-side distribution without charging a fresh SSH handshake."""
    submit_to_persistence_ms: list[float] = []
    persisted_to_all_receivers_ms: list[float] = []
    submit_to_all_receivers_ms: list[float] = []

    def run_marker(marker: bytes, *, record: bool) -> None:
        marker_text = marker.decode("ascii")
        tails = {receiver.name: b"" for receiver in receivers}
        rendered: set[str] = set()
        sender.drain()
        for receiver in receivers:
            receiver.drain()

        started_ns = time.monotonic_ns()
        sender.send(marker + b"\r")
        persisted_ns: int | None = None
        all_rendered_ns: int | None = None
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            persisted = matching_persisted_messages(log_path, marker_text)
            if len(persisted) > 1:
                raise BenchmarkError(
                    f"distribution marker was persisted more than once: {marker!r}"
                )
            if persisted == [marker_text] and persisted_ns is None:
                persisted_ns = time.monotonic_ns()

            for receiver in receivers:
                output = receiver.drain()
                probe = tails[receiver.name] + output
                if marker in probe:
                    rendered.add(receiver.name)
                tails[receiver.name] = probe[-max(0, len(marker) - 1) :]
            if len(rendered) == len(receivers) and all_rendered_ns is None:
                all_rendered_ns = time.monotonic_ns()

            if persisted_ns is not None and all_rendered_ns is not None:
                break
            if sender.process.poll() is not None:
                raise BenchmarkError(
                    "interactive sender disconnected during distribution run"
                )
            time.sleep(0.0002)

        if persisted_ns is None:
            raise BenchmarkError(
                f"distribution marker was not persisted exactly once: {marker!r}"
            )
        if all_rendered_ns is None:
            missing = sorted(
                receiver.name
                for receiver in receivers
                if receiver.name not in rendered
            )
            raise BenchmarkError(
                f"distribution marker was not rendered by receivers: {missing[:3]}"
            )
        # Persistence is completed before room_broadcast(). Scan the durable
        # log before draining receiver output in each probe iteration so the
        # external observation timestamps preserve that ordering as well.
        if all_rendered_ns < persisted_ns:
            raise BenchmarkError("distribution observation order was inconsistent")
        if record:
            submit_to_persistence_ms.append(
                (persisted_ns - started_ns) / 1_000_000
            )
            persisted_to_all_receivers_ms.append(
                (all_rendered_ns - persisted_ns) / 1_000_000
            )
            submit_to_all_receivers_ms.append(
                (all_rendered_ns - started_ns) / 1_000_000
            )

    run_marker(f"pdw{run_id[:4]}".encode(), record=False)
    for index in range(samples):
        run_marker(f"pd{run_id[:4]}{index:03d}".encode(), record=True)

    return {
        "submit_to_persistence_ms": summarize(submit_to_persistence_ms),
        "persisted_to_all_receivers_ms": summarize(
            persisted_to_all_receivers_ms
        ),
        "submit_to_all_receivers_ms": summarize(submit_to_all_receivers_ms),
        "joined_receivers": len(receivers),
        "messages": samples,
        "warmup_messages": 1,
        "deliveries_verified": len(receivers) * samples,
        "persistence_verified": samples,
        "completion": "every other joined TUI rendered the persisted marker",
        "sender_transport": "existing interactive SSH session",
        "gated_scope": (
            "first external observation of the flushed persisted record through "
            "marker render by every other joined TUI receiver"
        ),
        "observer_note": (
            "TNT persists before room broadcast; log and receiver observation "
            "add benchmark-loop scheduling overhead to the measured interval"
        ),
    }


def measure_slow_client(
    port: int,
    observer: InteractiveClient,
    pressure_chars: int,
    pressure_messages: int,
    samples: int,
    run_id: str,
) -> dict[str, Any]:
    """Keep one joined TUI unread while probing independent progress."""
    slow = InteractiveClient(
        port,
        f"slow-{run_id}",
        receive_buffer_bytes=4096,
    )
    try:
        slow.drain()
        slow.send(b"x" * pressure_chars)
        for index in range(pressure_messages):
            prefix = f"slow-pressure-{run_id}-{index:03d}-"
            message = prefix + ("x" * (900 - len(prefix)))
            result = ssh_exec(
                port,
                ["post", message],
                username="slow-pressure",
                timeout=5,
            )
            if result.returncode != 0 or result.stdout.strip() != b"posted":
                raise BenchmarkError(
                    f"slow-client pressure post failed at {index + 1}/{pressure_messages}"
                )
            observer.drain()
        time.sleep(0.25)

        health_ms: list[float] = []
        for _ in range(samples):
            started_ns = time.monotonic_ns()
            result = ssh_exec(port, ["health"], timeout=5)
            health_ms.append((time.monotonic_ns() - started_ns) / 1_000_000)
            if result.returncode != 0 or result.stdout.strip() != b"ok":
                raise BenchmarkError("health failed while a joined client was unread")

        observer.drain()
        render_ms: list[float] = []
        for index in range(samples):
            marker = f"slow-fast-{run_id}-{index:03d}".encode()
            started_ns = time.monotonic_ns()
            result = ssh_exec(
                port,
                ["post", marker.decode("ascii")],
                username="slow-probe",
                timeout=5,
            )
            if result.returncode != 0 or result.stdout.strip() != b"posted":
                raise BenchmarkError("post failed while a joined client was unread")
            if not observer.wait_for(marker, 5):
                raise BenchmarkError(
                    "responsive observer did not render while another client was unread"
                )
            render_ms.append((time.monotonic_ns() - started_ns) / 1_000_000)

        users_result = ssh_exec(port, ["users", "--json"], timeout=5)
        users = (
            set(json.loads(users_result.stdout))
            if users_result.returncode == 0
            else set()
        )
        return {
            "pressure_characters": pressure_chars,
            "pressure_messages": pressure_messages,
            "pressure_message_bytes": 900,
            "socket_receive_buffer_requested_bytes": 4096,
            "health_ms": summarize(health_ms),
            "responsive_observer_post_to_render_ms": summarize(render_ms),
            "slow_session_state": (
                "joined"
                if slow.name in users and slow.process.poll() is None
                else "bounded_disconnect"
            ),
            "server_healthy": users_result.returncode == 0,
            "progress_contract": (
                "health and an independent joined receiver remain responsive; "
                "the unread session may remain bounded or disconnect"
            ),
        }
    finally:
        slow.close()


def matching_persisted_messages(log_path: pathlib.Path, prefix: str) -> list[str]:
    try:
        lines = log_path.read_text(encoding="utf-8", errors="strict").splitlines()
    except FileNotFoundError:
        return []
    messages: list[str] = []
    for line in lines:
        fields = line.split("|", 2)
        if len(fields) == 3 and fields[2].startswith(prefix):
            messages.append(fields[2])
    return messages


def measure_ingest(
    sender: InteractiveClient,
    all_clients: Sequence[InteractiveClient],
    log_path: pathlib.Path,
    message_count: int,
    run_id: str,
) -> dict[str, Any]:
    prefix = f"perf-ingest-{run_id}-"
    expected = [f"{prefix}{index:06d}" for index in range(message_count)]
    payload = "\r".join(expected).encode("ascii") + b"\r"
    final_marker = expected[-1].encode("ascii")
    rendered_final: set[str] = set()
    render_tails = {client.name: b"" for client in all_clients}

    for client in all_clients:
        client.drain()
    started_ns = time.monotonic_ns()
    sender.send(payload)

    deadline = time.monotonic() + max(10.0, message_count / 20.0)
    observed: list[str] = []
    while time.monotonic() < deadline:
        for client in all_clients:
            output = client.drain()
            probe = render_tails[client.name] + output
            if final_marker in probe:
                rendered_final.add(client.name)
            render_tails[client.name] = probe[-max(0, len(final_marker) - 1) :]
        observed = matching_persisted_messages(log_path, prefix)
        if (len(observed) >= message_count and
                len(rendered_final) == len(all_clients)):
            break
        if sender.process.poll() is not None:
            raise BenchmarkError("interactive sender disconnected during ingest run")
        time.sleep(0.002)

    elapsed_ms = (time.monotonic_ns() - started_ns) / 1_000_000
    observed = matching_persisted_messages(log_path, prefix)
    complete = len(observed) == message_count
    ordered = observed == expected
    return {
        "messages_requested": message_count,
        "messages_persisted": len(observed),
        "elapsed_ms": round(elapsed_ms, 3),
        "messages_per_second": round(
            len(observed) * 1000.0 / elapsed_ms if elapsed_ms > 0 else 0.0, 3
        ),
        "complete": complete,
        "ordered": ordered,
        "sessions_rendered_final_message": len(rendered_final),
        "fanout_complete": len(rendered_final) == len(all_clients),
        "message_bytes": len(payload),
    }


def module_source_metadata(module_paths: str) -> dict[str, Any]:
    manifests: list[dict[str, str]] = []
    repositories: dict[str, dict[str, Any]] = {}
    for raw_path in module_paths.split(":"):
        path = pathlib.Path(raw_path).resolve()
        manifest_path = path / "tnt-module.json"
        try:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            raise BenchmarkError(f"cannot read module manifest {manifest_path}: {error}")
        manifests.append(
            {
                "directory": path.name,
                "name": str(manifest.get("name", "unknown")),
                "version": str(manifest.get("version", "unknown")),
            }
        )
        try:
            root = pathlib.Path(
                run_text(
                    ["git", "-C", str(path), "rev-parse", "--show-toplevel"],
                    check=True,
                )
            ).resolve()
        except (FileNotFoundError, subprocess.CalledProcessError):
            continue
        key = str(root)
        if key not in repositories:
            repositories[key] = {
                "directory": root.name,
                **git_metadata(root),
            }
    return {
        "manifests": manifests,
        "repositories": list(repositories.values()),
    }


def run_ingest_profile(
    binary: pathlib.Path,
    state_dir: pathlib.Path,
    *,
    client_count: int,
    message_count: int,
    run_id: str,
    module_paths: str | None,
) -> dict[str, Any]:
    server = start_server(
        binary,
        state_dir,
        max_connections=client_count + 16,
        module_paths=module_paths,
    )
    clients: list[InteractiveClient] = []
    try:
        wait_for_health(server.port)
        for index in range(client_count):
            clients.append(
                InteractiveClient(server.port, f"p{index:03d}-{run_id[-8:]}")
            )
        ingest = measure_ingest(
            clients[-1],
            clients,
            state_dir / "messages.log",
            message_count,
            run_id,
        )
        users_result = ssh_exec(server.port, ["users", "--json"], timeout=10)
        users = (
            set(json.loads(users_result.stdout))
            if users_result.returncode == 0
            else set()
        )
        expected = {client.name for client in clients}
        stderr_text = server.stderr_path.read_text(
            encoding="utf-8", errors="replace"
        )
        enabled_modules = re.findall(
            r"^module runtime: enabled (.+)$", stderr_text, flags=re.MULTILINE
        )
        module_error_lines = [
            line
            for line in stderr_text.splitlines()
            if "module runtime:" in line
            and any(
                marker in line.lower()
                for marker in ("failed", "disabled", "queue full", "timeout", "invalid")
            )
        ]
        profile = {
            "clients": client_count,
            "ingest": ingest,
            "sessions_survived": len(expected.intersection(users)),
            "server_healthy": ssh_exec(server.port, ["health"]).stdout.strip() == b"ok",
            "process_tree_resources": process_tree_resources(server.process.pid),
            "enabled_modules": enabled_modules,
            "module_error_lines": module_error_lines,
        }
        if not ingest["complete"] or not ingest["ordered"] or not ingest["fanout_complete"]:
            raise BenchmarkError(f"{run_id} ingest correctness failed")
        if profile["sessions_survived"] != client_count or not profile["server_healthy"]:
            raise BenchmarkError(f"{run_id} sessions or server did not survive")
        if module_error_lines:
            raise BenchmarkError(f"{run_id} module runtime reported errors")
        return profile
    finally:
        for client in reversed(clients):
            client.close()
        server.stop()


def measure_module_comparison(
    binary: pathlib.Path,
    host_key: pathlib.Path,
    module_paths: str,
    client_count: int,
    message_count: int,
    run_id: str,
) -> dict[str, Any]:
    profiles: dict[str, dict[str, Any]] = {}
    for label, paths in (("modules_off", None), ("modules_on", module_paths)):
        with tempfile.TemporaryDirectory(prefix=f"tnt-perf-{label}-") as temp:
            state_dir = pathlib.Path(temp)
            shutil.copy2(host_key, state_dir / "host_key")
            profiles[label] = run_ingest_profile(
                binary,
                state_dir,
                client_count=client_count,
                message_count=message_count,
                run_id=f"{run_id}-{'on' if paths else 'off'}",
                module_paths=paths,
            )

    expected_modules = len([path for path in module_paths.split(":") if path])
    enabled_modules = len(profiles["modules_on"]["enabled_modules"])
    if enabled_modules != expected_modules:
        raise BenchmarkError(
            "module comparison did not enable every requested module: "
            f"enabled={enabled_modules} expected={expected_modules}"
        )
    off_rate = profiles["modules_off"]["ingest"]["messages_per_second"]
    on_rate = profiles["modules_on"]["ingest"]["messages_per_second"]
    return {
        "status": "measured",
        "clients": client_count,
        "messages": message_count,
        "source": module_source_metadata(module_paths),
        **profiles,
        "throughput_ratio_modules_on_to_off": round(
            on_rate / off_rate if off_rate > 0 else 0.0, 3
        ),
    }


def measure_capacity_rejection(
    binary: pathlib.Path, state_dir: pathlib.Path
) -> dict[str, Any]:
    server = start_server(binary, state_dir, max_connections=1)
    holder: InteractiveClient | None = None
    try:
        wait_for_health(server.port)
        # The completed readiness session releases its slot asynchronously.
        deadline = time.monotonic() + 5
        while holder is None and time.monotonic() < deadline:
            try:
                holder = InteractiveClient(server.port, "capacity-holder", timeout=2)
            except BenchmarkError:
                time.sleep(0.02)
        if holder is None:
            raise BenchmarkError("could not occupy the single connection slot")

        started_ns = time.monotonic_ns()
        result = ssh_exec(server.port, ["health"], timeout=5)
        elapsed_ms = (time.monotonic_ns() - started_ns) / 1_000_000
        reason_logged = False
        log_deadline = time.monotonic() + 1
        while time.monotonic() < log_deadline:
            stderr_text = server.stderr_path.read_text(
                encoding="utf-8", errors="replace"
            )
            if "Max connections reached, rejecting" in stderr_text:
                reason_logged = True
                break
            time.sleep(0.01)
        client_stderr = result.stderr.decode("utf-8", errors="replace").strip()
        return {
            "limit": 1,
            "rejected": result.returncode != 0,
            "elapsed_ms": round(elapsed_ms, 3),
            "holder_survived": holder.process.poll() is None,
            "operator_reason_logged": reason_logged,
            "client_transport_feedback": client_stderr,
            "feedback_boundary": (
                "capacity is enforced before SSH key exchange; the standard client "
                "therefore reports a transport close while the explicit reason is "
                "recorded in the operator log"
            ),
        }
    finally:
        if holder is not None:
            holder.close()
        server.stop()


def metric_status(
    value: float | int | None, *, redline: float | int, higher_is_better: bool = False
) -> str:
    if value is None:
        return "not_measured"
    if higher_is_better:
        return "pass" if value >= redline else "fail"
    return "pass" if value <= redline else "fail"


def evaluate_budgets(metrics: dict[str, Any]) -> dict[str, dict[str, Any]]:
    values: dict[str, float | int | None] = {
        "existing_key_startup_p95_ms": metrics["existing_key_startup_ms"]["p95"],
        "idle_rss_kib": metrics["idle_server"]["rss_kib"],
        "sessions_64_rss_kib": (
            metrics["joined_sessions"]["resources"]["rss_kib"]
            if metrics["joined_sessions"]["joined"] == 64
            else None
        ),
        "ssh_handshake_p95_ms": metrics["ssh_health_handshake_ms"]["p95"],
        "persisted_to_all_receivers_p99_ms": metrics[
            "interactive_message_distribution"
        ]["persisted_to_all_receivers_ms"]["p99"],
        "message_ingest_per_second": metrics["message_ingest"][
            "messages_per_second"
        ],
        "main_binary_bytes": metrics["binary_bytes"]["tnt"],
    }
    result: dict[str, dict[str, Any]] = {}
    for name, budget in BUDGETS.items():
        value = values[name]
        higher_is_better = name == "message_ingest_per_second"
        result[name] = {
            "value": value,
            "ideal": budget["ideal"],
            "redline": budget["redline"],
            "higher_is_better": higher_is_better,
            "status": metric_status(
                value,
                redline=budget["redline"],
                higher_is_better=higher_is_better,
            ),
        }
    return result


def report_failed(report: dict[str, Any], scope: str) -> bool:
    for name, evaluation in report["budget_evaluation"].items():
        if scope == "stable" and name not in STABLE_GATE_METRICS:
            continue
        if evaluation["status"] != "pass":
            return True
    return False


def default_output(repo_root: pathlib.Path) -> pathlib.Path:
    stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    try:
        short_sha = run_text(
            ["git", "rev-parse", "--short=12", "HEAD"], cwd=repo_root, check=True
        )
    except (FileNotFoundError, subprocess.CalledProcessError):
        short_sha = "unknown"
    return repo_root / "perf-results" / f"{short_sha}-{stamp}.json"


def write_report(path: pathlib.Path, report: dict[str, Any]) -> None:
    """Atomically publish a report so CI never retains truncated JSON."""
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(report, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    temporary.replace(path)


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
        "generated_at": utc_now(),
        "status": "error",
        "error": {"type": type(error).__name__, "message": str(error)},
        "commit": git_metadata(repo_root),
        "environment": environment_metadata(binary),
        "configuration": {
            "startup_samples": args.startup_samples,
            "handshake_samples": args.handshake_samples,
            "fanout_samples": args.fanout_samples,
            "storm_clients": args.storm_clients,
            "clients": args.clients,
            "idle_seconds": args.idle_seconds,
            "messages": args.messages,
            "history_records": args.history_records,
            "slow_client_samples": args.slow_client_samples,
            "slow_client_characters": args.slow_client_characters,
            "slow_client_messages": args.slow_client_messages,
            "module_clients": args.module_clients,
            "module_messages": args.module_messages,
            "module_paths_configured": bool(args.module_paths),
            "server_wrapper": args.server_wrapper,
            "server_wrapper_argv": None,
            "expect_server_cpus": args.expect_server_cpus,
            "expect_memory_max_bytes": args.expect_memory_max_bytes,
            "expect_memory_swap_max_bytes": args.expect_memory_swap_max_bytes,
            "percentile_method": "nearest-rank",
            "enforce": args.enforce,
        },
        "metrics_complete": False,
        "budgets": BUDGETS,
    }


def validate_args(args: argparse.Namespace) -> None:
    integer_fields = (
        "startup_samples",
        "handshake_samples",
        "fanout_samples",
        "storm_clients",
        "clients",
        "messages",
        "history_records",
        "slow_client_samples",
        "slow_client_characters",
        "slow_client_messages",
        "module_clients",
        "module_messages",
    )
    for field in integer_fields:
        if getattr(args, field) < 1:
            raise BenchmarkError(f"--{field.replace('_', '-')} must be positive")
    if args.clients < 2:
        raise BenchmarkError("--clients must be at least 2 for peer fan-out")
    if (
        args.startup_samples < 5
        or args.handshake_samples < 5
        or args.fanout_samples < 5
        or args.storm_clients < 5
        or args.slow_client_samples < 5
    ):
        raise BenchmarkError(
            "startup, handshake, fanout, storm, and slow-client latency "
            "distributions require at least 5 samples"
        )
    if args.idle_seconds <= 0:
        raise BenchmarkError("--idle-seconds must be positive")
    if args.expect_server_cpus is not None and not args.expect_server_cpus.strip():
        raise BenchmarkError("--expect-server-cpus must not be empty")
    for field in ("expect_memory_max_bytes", "expect_memory_swap_max_bytes"):
        if getattr(args, field) is not None and getattr(args, field) < 0:
            raise BenchmarkError(f"--{field.replace('_', '-')} cannot be negative")
    if args.enforce == "all" and args.clients != 64:
        raise BenchmarkError("--enforce all requires exactly --clients 64")
    if args.module_paths:
        paths = [path for path in args.module_paths.split(":") if path]
        if not paths:
            raise BenchmarkError("--module-paths must contain at least one path")
        for raw_path in paths:
            path = pathlib.Path(raw_path)
            if not path.is_dir() or not (path / "tnt-module.json").is_file():
                raise BenchmarkError(f"invalid module path: {raw_path}")


def run_benchmark(args: argparse.Namespace) -> dict[str, Any]:
    repo_root = pathlib.Path(__file__).resolve().parent.parent
    binary = pathlib.Path(args.binary)
    if not binary.is_absolute():
        binary = (repo_root / binary).resolve()
    tntctl = binary.with_name("tntctl")
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise BenchmarkError(f"TNT binary is not executable: {binary}")
    if not shutil.which("ssh"):
        raise BenchmarkError("OpenSSH client (ssh) is required")
    server_wrapper = configure_server_wrapper(args.server_wrapper)

    run_id = uuid.uuid4().hex[:10]
    module_paths = None
    if args.module_paths:
        module_paths = ":".join(
            str(pathlib.Path(path).resolve())
            for path in args.module_paths.split(":")
            if path
        )
    metrics: dict[str, Any] = {
        "binary_bytes": {
            "tnt": binary.stat().st_size,
            "tntctl": tntctl.stat().st_size if tntctl.exists() else None,
        }
    }

    with tempfile.TemporaryDirectory(prefix="tnt-perf-startup-") as startup_tmp:
        startup_state = pathlib.Path(startup_tmp)
        first_start_ms, startup_samples = measure_startups(
            binary, startup_state, args.startup_samples
        )
        metrics["first_start_with_key_generation_ms"] = round(first_start_ms, 3)
        metrics["existing_key_startup_ms"] = summarize(startup_samples)

        history_log = startup_state / "messages.log"
        generate_history(history_log, args.history_records)
        cache_drop_requested = request_file_cache_drop(history_log)
        first_history_server = start_server(binary, startup_state)
        metrics["history_first_open_ms"] = {
            "elapsed_ms": round(first_history_server.startup_ms, 3),
            "records": args.history_records,
            "log_bytes": history_log.stat().st_size,
            "per_file_cache_drop_requested": cache_drop_requested,
        }
        first_history_server.stop()
        history_samples: list[float] = []
        for _ in range(args.startup_samples):
            server = start_server(binary, startup_state)
            history_samples.append(server.startup_ms)
            server.stop()
        metrics["history_startup_ms"] = {
            **summarize(history_samples),
            "records": args.history_records,
            "log_bytes": history_log.stat().st_size,
        }

        with tempfile.TemporaryDirectory(prefix="tnt-perf-storm-") as storm_tmp:
            storm_state = pathlib.Path(storm_tmp)
            shutil.copy2(startup_state / "host_key", storm_state / "host_key")
            metrics["connection_storm"] = measure_connection_storm(
                binary,
                storm_state,
                args.storm_clients,
                run_id,
            )

        with tempfile.TemporaryDirectory(prefix="tnt-perf-runtime-") as runtime_tmp:
            runtime_state = pathlib.Path(runtime_tmp)
            shutil.copy2(startup_state / "host_key", runtime_state / "host_key")
            server = start_server(
                binary,
                runtime_state,
                max_connections=max(128, args.clients + args.handshake_samples + 32),
            )
            clients: list[InteractiveClient] = []
            try:
                wait_for_health(server.port)
                metrics["server_constraints"] = process_constraints(
                    server.process.pid
                )
                require_process_constraints(
                    metrics["server_constraints"],
                    cpu_list=args.expect_server_cpus,
                    memory_max_bytes=args.expect_memory_max_bytes,
                    memory_swap_max_bytes=args.expect_memory_swap_max_bytes,
                )
                metrics["idle_server"] = process_resources(server.process.pid)
                metrics["ssh_health_handshake_ms"] = summarize(
                    measure_handshakes(server.port, args.handshake_samples)
                )

                for index in range(args.clients):
                    clients.append(
                        InteractiveClient(server.port, f"perf-client-{index:03d}")
                    )

                users_result = ssh_exec(server.port, ["users", "--json"])
                if users_result.returncode != 0:
                    raise BenchmarkError("users --json failed after client joins")
                users = json.loads(users_result.stdout)
                expected_users = {client.name for client in clients}
                joined_users = expected_users.intersection(users)
                metrics["joined_sessions"] = {
                    "requested": args.clients,
                    "joined": len(joined_users),
                    "confirmed_by": "post-room-add bracketed-paste marker and users --json",
                }
                if len(joined_users) != args.clients:
                    missing = sorted(expected_users - joined_users)
                    raise BenchmarkError(f"joined session verification failed: {missing}")

                metrics["joined_sessions"]["idle"] = measure_idle_cpu(
                    server, clients, args.idle_seconds
                )
                metrics["joined_sessions"]["resources"] = process_resources(
                    server.process.pid
                )
                metrics["interactive_message_distribution"] = (
                    measure_interactive_distribution(
                        clients[-1],
                        clients[:-1],
                        args.fanout_samples,
                        run_id,
                        runtime_state / "messages.log",
                    )
                )
                metrics["exec_post_to_all_receivers_ms"] = (
                    measure_exec_post_to_all_receivers(
                        server.port,
                        clients,
                        args.fanout_samples,
                        run_id,
                        runtime_state / "messages.log",
                    )
                )
                metrics["slow_client_backpressure"] = measure_slow_client(
                    server.port,
                    clients[0],
                    args.slow_client_characters,
                    args.slow_client_messages,
                    args.slow_client_samples,
                    run_id,
                )
                metrics["message_ingest"] = measure_ingest(
                    clients[-1],
                    clients,
                    runtime_state / "messages.log",
                    args.messages,
                    run_id,
                )
                survivors_result = ssh_exec(server.port, ["users", "--json"])
                if survivors_result.returncode != 0:
                    raise BenchmarkError("users --json failed after ingest run")
                surviving_users = set(json.loads(survivors_result.stdout))
                surviving_expected = expected_users.intersection(surviving_users)
                metrics["message_ingest"]["joined_sessions_survived"] = len(
                    surviving_expected
                )
                metrics["message_ingest"]["server_healthy"] = (
                    ssh_exec(server.port, ["health"]).stdout.strip() == b"ok"
                )
                metrics["post_ingest_resources"] = process_resources(
                    server.process.pid
                )
                if not metrics["message_ingest"]["complete"]:
                    raise BenchmarkError("ingest run did not persist every message")
                if not metrics["message_ingest"]["ordered"]:
                    raise BenchmarkError("ingest run changed message order")
                if not metrics["message_ingest"]["fanout_complete"]:
                    raise BenchmarkError(
                        "ingest final message did not render for every joined session"
                    )
                if len(surviving_expected) != args.clients:
                    missing = sorted(expected_users - surviving_expected)
                    raise BenchmarkError(
                        f"interactive sessions disconnected during ingest: {missing}"
                    )
                if not metrics["message_ingest"]["server_healthy"]:
                    raise BenchmarkError("server health failed after ingest run")
            finally:
                for client in reversed(clients):
                    client.close()
                server.stop()

        if module_paths:
            metrics["module_comparison"] = measure_module_comparison(
                binary,
                startup_state / "host_key",
                module_paths,
                args.module_clients,
                args.module_messages,
                run_id,
            )
        else:
            metrics["module_comparison"] = {
                "status": "not_configured",
                "reason": (
                    "pass --module-paths with one or more colon-separated "
                    "validated module directories"
                ),
            }

        with tempfile.TemporaryDirectory(prefix="tnt-perf-capacity-") as capacity_tmp:
            capacity_state = pathlib.Path(capacity_tmp)
            shutil.copy2(startup_state / "host_key", capacity_state / "host_key")
            metrics["capacity_rejection"] = measure_capacity_rejection(
                binary, capacity_state
            )
            if not metrics["capacity_rejection"]["rejected"]:
                raise BenchmarkError("capacity scenario accepted a connection above the limit")
            if not metrics["capacity_rejection"]["holder_survived"]:
                raise BenchmarkError("capacity rejection disconnected an existing session")
            if not metrics["capacity_rejection"]["operator_reason_logged"]:
                raise BenchmarkError("capacity rejection did not log an explicit reason")

    report: dict[str, Any] = {
        "schema_version": SCHEMA_VERSION,
        "generated_at": utc_now(),
        "commit": git_metadata(repo_root),
        "environment": environment_metadata(binary),
        "configuration": {
            "startup_samples": args.startup_samples,
            "handshake_samples": args.handshake_samples,
            "fanout_samples": args.fanout_samples,
            "storm_clients": args.storm_clients,
            "clients": args.clients,
            "idle_seconds": args.idle_seconds,
            "messages": args.messages,
            "history_records": args.history_records,
            "slow_client_samples": args.slow_client_samples,
            "slow_client_characters": args.slow_client_characters,
            "slow_client_messages": args.slow_client_messages,
            "module_clients": args.module_clients,
            "module_messages": args.module_messages,
            "module_paths_configured": bool(module_paths),
            "server_wrapper": args.server_wrapper,
            "server_wrapper_argv": server_wrapper,
            "expect_server_cpus": args.expect_server_cpus,
            "expect_memory_max_bytes": args.expect_memory_max_bytes,
            "expect_memory_swap_max_bytes": args.expect_memory_swap_max_bytes,
            "percentile_method": "nearest-rank",
            "enforce": args.enforce,
        },
        "metrics": metrics,
        "measurement_contract": {
            "startup": "process spawn to TNT listening announcement",
            "handshake": (
                "fresh OpenSSH process through authenticated health response"
            ),
            "joined_session": (
                "authentication, username submission, room add, and "
                "bracketed-paste marker emitted"
            ),
            "connection_storm": (
                "thread-barrier synchronized OpenSSH clients through "
                "confirmed room join"
            ),
            "distribution": (
                "flushed persistence observation through marker render by "
                "every other joined TUI receiver"
            ),
            "conservative_fanout": (
                "fresh SSH exec post start through persistence and marker "
                "render by every joined receiver"
            ),
            "ingest": (
                "interactive payload write to ordered persistence and "
                "final-message render in every joined session"
            ),
            "slow_client": (
                "health and responsive-receiver latency while one joined TUI "
                "is deliberately unread"
            ),
            "modules": (
                "identical ordered ingest profile with configured external "
                "modules disabled and enabled"
            ),
            "memory": (
                "RSS and virtual memory are reported separately; thread "
                "stacks are not treated as resident"
            ),
            "server_wrapper": (
                "optional exec prefix constrains only TNT and inherited "
                "module processes; it is recorded verbatim"
            ),
        },
        "budgets": BUDGETS,
    }
    report["budget_evaluation"] = evaluate_budgets(metrics)
    return report


def self_test() -> int:
    summary = summarize([5, 1, 4, 2, 3])
    assert summary["p50"] == 3
    assert summary["p95"] == 5
    assert summary["p99"] == 5
    assert metric_status(10, redline=10) == "pass"
    assert metric_status(11, redline=10) == "fail"
    assert metric_status(10, redline=10, higher_is_better=True) == "pass"
    assert metric_status(9, redline=10, higher_is_better=True) == "fail"
    assert metric_status(None, redline=10) == "not_measured"
    require_process_constraints(
        {
            "cpu_allowed_list": "0",
            "memory_max_bytes": 128 * MIB,
            "memory_swap_max_bytes": 0,
        },
        cpu_list="0",
        memory_max_bytes=128 * MIB,
        memory_swap_max_bytes=0,
    )
    try:
        require_process_constraints(
            {"cpu_allowed_list": "1"},
            cpu_list="0",
            memory_max_bytes=None,
            memory_swap_max_bytes=None,
        )
    except BenchmarkError:
        pass
    else:
        raise AssertionError("constraint mismatch was not rejected")
    missing_report = {
        "budget_evaluation": {
            name: {"status": "not_measured" if name == "idle_rss_kib" else "pass"}
            for name in BUDGETS
        }
    }
    assert report_failed(missing_report, "stable")
    print("perf_benchmark self-test passed")
    return 0


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run TNT's reproducible real-client performance benchmark."
    )
    parser.add_argument("--binary", default="./tnt", help="TNT server binary")
    parser.add_argument("--output", help="JSON output path")
    parser.add_argument("--startup-samples", type=int, default=21)
    parser.add_argument("--handshake-samples", type=int, default=11)
    parser.add_argument("--fanout-samples", type=int, default=11)
    parser.add_argument("--storm-clients", type=int, default=8)
    parser.add_argument("--clients", type=int, default=8)
    parser.add_argument("--idle-seconds", type=float, default=2.0)
    parser.add_argument("--messages", type=int, default=100)
    parser.add_argument("--history-records", type=int, default=100_000)
    parser.add_argument("--slow-client-samples", type=int, default=5)
    parser.add_argument("--slow-client-characters", type=int, default=3200)
    parser.add_argument("--slow-client-messages", type=int, default=16)
    parser.add_argument("--module-paths")
    parser.add_argument("--module-clients", type=int, default=8)
    parser.add_argument("--module-messages", type=int, default=100)
    parser.add_argument(
        "--server-wrapper",
        help="shell-like exec prefix for target-host constraints (for example taskset/prlimit)",
    )
    parser.add_argument(
        "--expect-server-cpus",
        help="require the TNT process Cpus_allowed_list to equal this value",
    )
    parser.add_argument(
        "--expect-memory-max-bytes",
        type=int,
        help="require the TNT cgroup-v2 memory.max value",
    )
    parser.add_argument(
        "--expect-memory-swap-max-bytes",
        type=int,
        help="require the TNT cgroup-v2 memory.swap.max value",
    )
    parser.add_argument(
        "--enforce",
        choices=("none", "stable", "all"),
        default="none",
        help="exit non-zero when the selected budget scope crosses a redline",
    )
    parser.add_argument("--self-test", action="store_true")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv or sys.argv[1:])
    if args.self_test:
        return self_test()

    output: pathlib.Path | None = None
    repo_root = pathlib.Path(__file__).resolve().parent.parent
    try:
        validate_args(args)
        output = pathlib.Path(args.output) if args.output else default_output(repo_root)
        if not output.is_absolute():
            output = (repo_root / output).resolve()
        report = run_benchmark(args)
        report["status"] = "ok"
        write_report(output, report)
        json.dump(report, sys.stdout, ensure_ascii=False, sort_keys=True)
        sys.stdout.write("\n")
        print(f"performance report: {output}", file=sys.stderr)
        if args.enforce != "none" and report_failed(report, args.enforce):
            print(f"performance redline crossed ({args.enforce} scope)", file=sys.stderr)
            return 1
        return 0
    except (BenchmarkError, OSError, subprocess.SubprocessError, json.JSONDecodeError) as error:
        if output is not None:
            try:
                write_report(output, failure_report(args, repo_root, error))
                print(f"performance diagnostic report: {output}", file=sys.stderr)
            except (OSError, subprocess.SubprocessError) as report_error:
                print(
                    f"perf_benchmark: could not write diagnostic report: {report_error}",
                    file=sys.stderr,
                )
        print(f"perf_benchmark: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
