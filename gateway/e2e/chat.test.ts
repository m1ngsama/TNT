import { afterAll, beforeAll, describe, expect, test } from "bun:test";
import { mkdtempSync, openSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import type { Subprocess } from "bun";
import { SignJWT, exportJWK, generateKeyPair } from "jose";
import { type Frame, connect } from "../test/client";

const enabled = process.env.TNT_E2E === "1";
const sshPort = Number(process.env.PORT || 12369);
const wsPort = sshPort + 1000;
const url = `ws://127.0.0.1:${wsPort}/ws`;
const repo = resolve(import.meta.dir, "../..");

let stateDir = "";
let issuer = "";
let jwks: ReturnType<typeof Bun.serve>;
let tnt: Subprocess | undefined;
let privateKey: CryptoKey;
let carolMessage: Frame = {};

function token(handle: string, audience = ["vibecloud", "chan", "tnt"]): Promise<string> {
  return new SignJWT({ handle, token_use: "access" })
    .setProtectedHeader({ alg: "EdDSA", kid: "e2e" })
    .setIssuer(issuer)
    .setAudience(audience)
    .setSubject(`user-${handle}`)
    .setIssuedAt()
    .setExpirationTime("10m")
    .sign(privateKey);
}

async function waitForLog(log: string, pattern: string) {
  const deadline = Date.now() + 15_000;
  while (!readFileSync(log, "utf8").includes(pattern)) {
    if (Date.now() > deadline) throw new Error(`${log} never showed "${pattern}":\n${readFileSync(log, "utf8")}`);
    await Bun.sleep(50);
  }
}

async function startTnt(name: string) {
  const log = join(stateDir, `${name}.log`);
  const fd = openSync(log, "w");
  tnt = Bun.spawn([join(repo, "tnt"), "--bind", "127.0.0.1", "-p", String(sshPort), "-d", stateDir], {
    env: {
      ...process.env,
      TNT_LANG: "en",
      TNT_KEYMAP: "default",
      TNT_RATE_LIMIT: "0",
      TNT_MODULE_PATHS: join(repo, "gateway"),
      TNT_MODULE_GRANTS: "tnt-gateway",
      TNT_GATEWAY_PORT: String(wsPort),
      TNT_GATEWAY_ISSUER: issuer,
    },
    stdout: fd,
    stderr: fd,
  });
  await waitForLog(log, "module runtime: enabled tnt-gateway");
  await waitForLog(log, "TNT chat server listening");
}

async function stopTnt() {
  tnt?.kill("SIGTERM");
  await tnt?.exited;
  tnt = undefined;
}

describe.skipIf(!enabled)("TNT with the gateway module", () => {
  beforeAll(async () => {
    stateDir = mkdtempSync(join(tmpdir(), "tnt-gateway-e2e-"));
    const pair = await generateKeyPair("EdDSA");
    privateKey = pair.privateKey;
    const jwk = { ...(await exportJWK(pair.publicKey)), kid: "e2e", alg: "EdDSA" };
    jwks = Bun.serve({ hostname: "127.0.0.1", port: 0, fetch: () => Response.json({ keys: [jwk] }) });
    issuer = `http://127.0.0.1:${jwks.port}`;
    await startTnt("server-1");
  });

  afterAll(async () => {
    await stopTnt();
    void jwks?.stop(true);
    rmSync(stateDir, { recursive: true, force: true });
  });

  test("web and ssh users see each other's messages and presence", async () => {
    const alice = connect(url, await token("alice"));
    expect(await alice.next((f) => f.type === "ready", 10_000)).toMatchObject({
      room: "lobby",
      you: "alice",
      presence: ["alice"],
    });

    const done = join(stateDir, "ssh.done");
    const script = join(stateDir, "carol.expect");
    writeFileSync(
      script,
      `set timeout 20
spawn ssh -e none -tt -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -p ${sshPort} anonymous@127.0.0.1
expect "): "
send -- "carol\\r"
expect "/help"
expect "hello from the web"
send -- "hello from ssh\\r"
exec sh -c "while \\[ ! -f '${done}' \\]; do sleep 0.1; done"
close
`,
    );
    const ssh = Bun.spawn(["expect", "-f", script], { stdout: "pipe", stderr: "pipe" });

    await alice.next((f) => f.type === "presence" && f.online.includes("carol"), 20_000);
    alice.send({ type: "send", text: "hello from the web" });
    const own = await alice.next((f) => f.type === "message" && f.sender === "alice");
    expect(own).toMatchObject({ room: "lobby", text: "hello from the web" });
    expect(own.id).toMatch(/^[0-9a-f]{16}$/);

    const fromSsh = await alice.next((f) => f.type === "message" && f.sender === "carol", 20_000);
    expect(fromSsh.text).toBe("hello from ssh");
    const { type: _type, ...message } = fromSsh;
    carolMessage = message;

    writeFileSync(done, "");
    const exitCode = await ssh.exited;
    if (exitCode !== 0) console.error(await new Response(ssh.stdout).text());
    expect(exitCode).toBe(0);
    await alice.next((f) => f.type === "presence" && !f.online.includes("carol"), 10_000);

    expect(readFileSync(join(stateDir, "messages.log"), "utf8")).toContain("|alice|hello from the web\n");

    const bob = connect(url, await token("bob"));
    const ready = await bob.next((f) => f.type === "ready", 10_000);
    expect(ready.presence).toEqual(["alice", "bob"]);
    expect(ready.history).toContainEqual(carolMessage);
    alice.ws.close();
    bob.ws.close();
  }, 60_000);

  test("a restarted gateway rebuilds the same ids from messages.log", async () => {
    await stopTnt();
    await startTnt("server-2");
    const erin = connect(url, await token("erin"));
    const ready = await erin.next((f) => f.type === "ready", 10_000);
    expect(ready.history).toContainEqual(carolMessage);
    expect(ready.history.map((m: Frame) => m.text)).toContain("hello from the web");
    erin.ws.close();
  }, 60_000);

  test("a token without the tnt audience is closed with 4401", async () => {
    expect(await connect(url, await token("mallory", ["chan"])).closed).toBe(4401);
  }, 20_000);
});
