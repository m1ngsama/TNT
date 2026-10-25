#!/bin/sh
set -eu

python3 - "${TNT_BIN:-../tnt}" "${PORT:-13820}" <<'PY'
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

binary = str(Path(sys.argv[1]).resolve())
port = sys.argv[2]
ssh = ["ssh", "-T", "-e", "none", "-p", port, "-o", "BatchMode=yes",
       "-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null",
       "-o", "LogLevel=ERROR", "-o", "PubkeyAuthentication=no",
       "-o", "SendEnv=-LANG", "-o", "SendEnv=-LC_*", "anonymous@127.0.0.1"]

def wait_for(predicate, description, seconds=8):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.03)
    raise AssertionError(description)

with tempfile.TemporaryDirectory(prefix="tnt-moderation.") as directory:
    state = Path(directory)
    server = None
    clients = []
    log = (state / "server.log").open("w+")
    env = dict(os.environ, TNT_LANG="en", TNT_RATE_LIMIT="1",
               TNT_MAX_CONN_RATE_PER_IP="1024", TNT_MAX_CONN_PER_IP="10")
    def start():
        process = subprocess.Popen([binary, "--bind", "127.0.0.1", "-p", port,
                                    "-d", directory], env=env, stdout=log, stderr=log)
        wait_for(lambda: "TNT chat server listening" in (state / "server.log").read_text(),
                 "server did not start", 15)
        return process

    def audit():
        path = state / "security.log"
        if not path.exists():
            return []
        return [json.loads(line) for line in path.read_text().splitlines() if line.endswith("}")]

    def has_event(event, nickname):
        return any(row["event"] == event and row["nickname"] == nickname for row in audit())

    def history():
        path = state / "messages.log"
        return path.read_text() if path.exists() else ""

    def join(name):
        process = subprocess.Popen(ssh, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL)
        clients.append(process)
        process.stdin.write(name.encode() + b"\r")
        process.stdin.flush()
        wait_for(lambda: has_event("session_join", name), "anonymous join failed: " + name)
        return process

    def send(process, content):
        process.stdin.write(content)
        process.stdin.flush()

    def close(process):
        process.terminate()
        process.wait(timeout=5)
        process.stdin.close()

    def command(*args):
        return subprocess.run([binary, "-d", directory, *args], capture_output=True,
                              text=True, timeout=5, check=True)

    fixture = state / "policy"
    fixture.mkdir()
    (fixture / "tnt-module.json").write_text(json.dumps({
        "protocol": "tnt.module.v1", "name": "test-policy", "entrypoint": "./policy.py",
        "permissions": ["moderation:write"], "events": ["moderation.check"]}))
    (fixture / "policy.py").write_text("""#!/usr/bin/env python3
import json, os, sys, time
for line in sys.stdin:
    event = json.loads(line)
    if event['type'] == 'handshake':
        result = {'type': 'handshake.ok', 'protocol': 'tnt.module.v1'}
    else:
        with open(os.environ['TNT_STATE_DIR'] + '/policy-events', 'a') as stream:
            stream.write(json.dumps(event) + '\\n')
        text = event.get('plain_text', '')
        if text == 'stall-me': time.sleep(2)
        action = 'ban' if text == 'ban-me' else 'reject' if text == 'reject-me' else 'allow'
        result = {'type': 'moderation.result', 'id': event['id'], 'action': action,
                  'reason': 'test_policy', 'notice': 'Rejected by test policy.' if action != 'allow' else '',
                  'notice_delay_ms': 0, 'ban_seconds': 60}
    print(json.dumps(result), flush=True)
""")
    (fixture / "policy.py").chmod(0o755)
    env.update(TNT_MODULE_PATHS=str(fixture), TNT_STATE_DIR=directory)

    try:
        server = start()
        client = join("?")
        send(client, b"\n\r")
        wait_for(lambda: "|?|\\n|" in history(), "core applied a content policy without a granted module")
        close(client)
        assert not (state / "policy-events").exists(), "ungranted policy received private context"
        print("✓ core has no nickname or spam policy and ungranted modules get no source events")

        server.terminate()
        server.wait(timeout=5)
        env["TNT_MODULE_GRANTS"] = "test-policy"
        log.seek(0)
        log.truncate()
        server = start()
        client = join("anonymous-reader")
        send(client, b"/msg missing-user PRIVATE-BODY-SENTINEL\r")
        wait_for(lambda: (state / "policy-events").exists(), "granted policy did not receive events")
        send(client, b"\x03reject-me\r")
        wait_for(lambda: has_event("message_rejected", "anonymous-reader"), "policy rejection not applied")
        assert "|anonymous-reader|reject-me|" not in history()
        send(client, b"\x03ban-me\r")
        client.wait(timeout=5)
        client.stdin.close()
        assert command("--list-bans").stdout.startswith("127.0.0.1 ")
        print("✓ granted policy can reject before persistence and ban the source")

        server.terminate()
        server.wait(timeout=5)
        log.seek(0)
        log.truncate()
        server = start()
        blocked = subprocess.run(ssh + ["health"], capture_output=True, timeout=5)
        assert blocked.returncode != 0, "restart lost ban"
        command("--unban", "127.0.0.1")
        health = subprocess.run(ssh + ["health"], capture_output=True, timeout=5)
        assert health.stdout.strip() == b"ok", "operator unban did not take effect"
        print("✓ owner bans survive restart and unban applies live")

        client = join("idle-reader")
        command("--ban", "127.0.0.1", "--ban-seconds", "60")
        client.wait(timeout=4)
        client.stdin.close()
        command("--unban", "127.0.0.1")
        print("✓ manual bans disconnect existing idle sessions")

        client = join("policy-timeout")
        send(client, b"stall-me\r")
        wait_for(lambda: "|policy-timeout|stall-me|" in history(), "failed policy blocked basic chat")
        assert has_event("policy_unavailable", "policy-timeout"), "policy failure was silent"
        close(client)
        print("✓ unhealthy policy is disabled and audited without stopping basic chat")

        events = [json.loads(line) for line in (state / "policy-events").read_text().splitlines()]
        private = [event for event in events if event.get("scope") == "private"]
        assert private and all("plain_text" not in event for event in private)
        assert "PRIVATE-BODY-SENTINEL" not in (state / "security.log").read_text()
        assert (state / "security.log").stat().st_mode & 0o777 == 0o600
        assert all("127.0.0.1" not in line for line in history().splitlines())
        print("✓ private bodies are excluded from audit and policy context")
    except BaseException:
        print((state / "server.log").read_text(), file=sys.stderr)
        print(history(), file=sys.stderr)
        raise
    finally:
        for process in clients:
            if process.poll() is None:
                process.kill()
            process.wait()
            if process.stdin and not process.stdin.closed:
                process.stdin.close()
        if server and server.poll() is None:
            server.terminate()
            server.wait(timeout=5)
        log.close()
PY
