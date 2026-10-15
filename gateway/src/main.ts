import { createVerifier, remoteKeys } from "./auth";
import { createGateway } from "./gateway";
import { createReporter } from "./events";
import { createGuard } from "./guard";
import { Ledger, readHistory } from "./history";
import { signIn } from "./identity";
import { encodePost, encodeRecord, parseTntLine, supportsIdentity, supportsPrivate } from "./module";

const HISTORY_LIMIT = 5000;
const env = process.env;
const stateDir = env.TNT_STATE_DIR || ".";
const issuer = env.TNT_GATEWAY_ISSUER || "https://auth.m1ng.space";
const stdout = Bun.stdout.writer();
const write = (line: string) => {
  stdout.write(line);
  stdout.flush();
};

const ledger = new Ledger(HISTORY_LIMIT);
for (const entry of await readHistory(stateDir, HISTORY_LIMIT)) ledger.add(entry);

const verify = createVerifier(issuer, remoteKeys(env.TNT_GATEWAY_JWKS_URL || `${issuer}/oidc/jwks`));
let identity = false;
let direct = false;
const signingIn = new Set<string>();
const eventsSecret = env.TNT_GATEWAY_EVENTS_SECRET;
const reporter = eventsSecret ? createReporter({ issuer, secret: eventsSecret, file: `${stateDir}/gateway-events.json` }) : null;
if (reporter) {
  await reporter.load();
  reporter.start();
}
const guard = createGuard({
  issuer,
  reserve: (handle) => {
    if (identity) write(encodeRecord({ type: "identity.reserve", handle }));
  },
});

const gateway = createGateway({
  ledger,
  verify,
  post: (sender, text) => write(encodePost(sender, text)),
  privatePost: (from, to, plain_text) => {
    if (direct) write(encodeRecord({ type: "private.post", from, to, plain_text }));
    return direct;
  },
  remotePresence: (nicknames) => {
    if (direct) write(encodeRecord({ type: "presence.remote", nicknames }));
  },
  signedIn: (handle) => guard.reserve(handle),
});

setInterval(() => gateway.sweep(), 60_000);

Bun.serve({
  hostname: env.TNT_GATEWAY_HOST || "127.0.0.1",
  port: Number(env.TNT_GATEWAY_PORT || 8787),
  fetch: gateway.fetch,
  websocket: gateway.websocket,
});

for await (const line of console) {
  if (line === "") continue;
  const { reply, event } = parseTntLine(line);
  if (reply) write(reply);
  if (!event) continue;
  if (event.type === "handshake") {
    identity = supportsIdentity(event.version);
    direct = supportsPrivate(event.version);
  } else if (event.type === "private.created") {
    gateway.handlePrivate(event);
  } else if (event.type === "identity.changed") {
    void reporter?.report({ handle: event.handle, type: `tnt.${event.action}`, detail: event.detail });
  } else if (event.type === "identity.requested") {
    const { session } = event;
    if (!identity || signingIn.has(session)) continue;
    signingIn.add(session);
    void signIn({
      issuer,
      verify,
      notice: (plain_text) => write(encodeRecord({ type: "identity.notice", session, plain_text })),
      verified: (handle) => {
        write(encodeRecord({ type: "identity.verified", session, handle }));
        void reporter?.report({ handle, type: "tnt.signed_in", detail: { method: "device_code" } });
      },
    }).finally(() => signingIn.delete(session));
  } else {
    if (identity && event.type === "presence.joined") void guard.check(event.nickname);
    if (identity && event.type === "presence.snapshot") for (const nickname of event.nicknames) void guard.check(nickname);
    gateway.handleEvent(event);
  }
}
process.exit(0);
