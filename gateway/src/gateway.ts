import type { Server, ServerWebSocket } from "bun";
import type { Verify } from "./auth";
import { type Ledger, type Message, ROOM } from "./history";
import type { TntEvent } from "./module";
import { Presence } from "./presence";
import { RateLimiter } from "./ratelimit";
import { normalizeText, validateText } from "./text";

export const CLOSE_UNAUTHORIZED = 4401;
const AUTH_TIMEOUT_MS = 5_000;
const OPEN = 1;

type Session = {
  id: string;
  state: "new" | "pending" | "ready";
  handle: string;
  timer?: ReturnType<typeof setTimeout>;
};
type Socket = ServerWebSocket<Session>;
type Frame = Record<string, unknown> | null;

export type GatewayOptions = {
  ledger: Ledger;
  verify: Verify;
  post: (sender: string, text: string) => void;
  now?: () => Date;
  authTimeoutMs?: number;
};

function parseFrame(raw: string | Buffer): Frame {
  if (typeof raw !== "string") return null;
  try {
    const value: unknown = JSON.parse(raw);
    return typeof value === "object" && value !== null ? (value as Record<string, unknown>) : null;
  } catch {
    return null;
  }
}

export function createGateway(options: GatewayOptions) {
  const presence = new Presence();
  const limiter = new RateLimiter(3, 1);
  const clients = new Set<Socket>();
  const now = options.now ?? (() => new Date());
  let online = "[]";

  const broadcast = (frame: object) => {
    const data = JSON.stringify(frame);
    for (const ws of clients) ws.send(data);
  };
  const deliver = (message: Message) => broadcast({ type: "message", ...message });
  const syncPresence = () => {
    const list = presence.online();
    const key = JSON.stringify(list);
    if (key === online) return;
    online = key;
    broadcast({ type: "presence", room: ROOM, online: list });
  };
  const reject = (ws: Socket) => ws.close(CLOSE_UNAUTHORIZED, "unauthorized");
  const fail = (ws: Socket, code: string) => ws.send(JSON.stringify({ type: "error", code }));

  async function authenticate(ws: Socket, frame: Frame) {
    if (frame?.type !== "auth" || typeof frame.token !== "string") return reject(ws);
    ws.data.state = "pending";
    const handle = await options.verify(frame.token);
    if (ws.readyState !== OPEN) return;
    if (handle === null) return reject(ws);
    clearTimeout(ws.data.timer);
    ws.data.state = "ready";
    ws.data.handle = handle;
    presence.webJoined(handle, ws.data.id);
    syncPresence();
    ws.send(
      JSON.stringify({ type: "ready", room: ROOM, you: handle, history: options.ledger.recent(), presence: presence.online() }),
    );
    clients.add(ws);
  }

  function receive(ws: Socket, frame: Frame) {
    if (frame?.type === "ping") return ws.send('{"type":"pong"}');
    if (frame?.type !== "send") return fail(ws, "invalid_frame");
    const text = validateText(frame.text);
    if (text === null) return fail(ws, "invalid_text");
    const sender = ws.data.handle;
    if (!limiter.take(sender)) return fail(ws, "rate_limited");
    options.post(sender, text);
    const timestamp = now().toISOString().replace(/\.\d{3}Z$/, "Z");
    deliver(options.ledger.add({ timestamp, sender, text }));
  }

  return {
    fetch(request: Request, server: Server<Session>): Response | undefined {
      if (new URL(request.url).pathname !== "/ws") return new Response("Not Found", { status: 404 });
      if (server.upgrade(request, { data: { id: crypto.randomUUID(), state: "new", handle: "" } })) return undefined;
      return new Response("Upgrade Required", { status: 426 });
    },
    websocket: {
      maxPayloadLength: 16 * 1024,
      open(ws: Socket) {
        ws.data.timer = setTimeout(() => reject(ws), options.authTimeoutMs ?? AUTH_TIMEOUT_MS);
      },
      message(ws: Socket, raw: string | Buffer) {
        const frame = parseFrame(raw);
        if (ws.data.state === "new") void authenticate(ws, frame);
        else if (ws.data.state === "ready") receive(ws, frame);
      },
      close(ws: Socket) {
        clearTimeout(ws.data.timer);
        if (!clients.delete(ws)) return;
        presence.webLeft(ws.data.handle, ws.data.id);
        syncPresence();
      },
    },
    sweep(at?: number) {
      limiter.sweep(at);
    },
    handleEvent(event: TntEvent) {
      if (event.type === "message.created") {
        const { timestamp, sender, plain_text } = event.message;
        deliver(options.ledger.add({ timestamp, sender, text: normalizeText(plain_text) }));
        return;
      }
      if (event.type === "presence.snapshot") presence.sshSnapshot(event.nicknames);
      else if (event.type === "presence.joined") presence.sshJoined(event.nickname);
      else presence.sshLeft(event.nickname);
      syncPresence();
    },
  };
}
