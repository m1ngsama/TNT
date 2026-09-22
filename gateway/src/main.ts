import { createVerifier, remoteKeys } from "./auth";
import { createGateway } from "./gateway";
import { Ledger, readHistory } from "./history";
import { encodePost, parseTntLine } from "./module";

const HISTORY_LIMIT = 100;
const env = process.env;
const issuer = env.TNT_GATEWAY_ISSUER || "https://auth.m1ng.space";
const stdout = Bun.stdout.writer();
const write = (line: string) => {
  stdout.write(line);
  stdout.flush();
};

const ledger = new Ledger(HISTORY_LIMIT);
for (const entry of await readHistory(env.TNT_STATE_DIR || ".", HISTORY_LIMIT)) ledger.add(entry);

const gateway = createGateway({
  ledger,
  verify: createVerifier(issuer, remoteKeys(env.TNT_GATEWAY_JWKS_URL || `${issuer}/oidc/jwks`)),
  post: (sender, text) => write(encodePost(sender, text)),
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
  if (event) gateway.handleEvent(event);
}
process.exit(0);
