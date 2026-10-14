import { createVerifier, remoteKeys } from "./auth";
import { createGateway } from "./gateway";
import { Ledger, markReserved, readHistory, readReserved } from "./history";
import { signIn } from "./identity";
import { encodePost, encodeRecord, parseTntLine, supportsIdentity } from "./module";

const HISTORY_LIMIT = 5000;
const env = process.env;
const stateDir = env.TNT_STATE_DIR || ".";
const issuer = env.TNT_GATEWAY_ISSUER || "https://auth.m1ng.space";
const stdout = Bun.stdout.writer();
const write = (line: string) => {
  stdout.write(line);
  stdout.flush();
};

const reserved = await readReserved(stateDir);
const ledger = new Ledger(HISTORY_LIMIT);
for (const entry of await readHistory(stateDir, HISTORY_LIMIT)) ledger.add(markReserved(entry, reserved));

const verify = createVerifier(issuer, remoteKeys(env.TNT_GATEWAY_JWKS_URL || `${issuer}/oidc/jwks`));
let identity = false;
const signingIn = new Set<string>();

const gateway = createGateway({
  ledger,
  verify,
  post: (sender, text) => write(encodePost(sender, text)),
  signedIn: (handle) => {
    if (!identity || reserved.has(handle)) return;
    reserved.add(handle);
    write(encodeRecord({ type: "identity.reserve", handle }));
  },
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
  } else if (event.type === "identity.requested") {
    const { session } = event;
    if (!identity || signingIn.has(session)) continue;
    signingIn.add(session);
    void signIn({
      issuer,
      verify,
      notice: (plain_text) => write(encodeRecord({ type: "identity.notice", session, plain_text })),
      verified: (handle) => {
        reserved.add(handle);
        write(encodeRecord({ type: "identity.verified", session, handle }));
      },
    }).finally(() => signingIn.delete(session));
  } else {
    gateway.handleEvent(event);
  }
}
process.exit(0);
