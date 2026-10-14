import type { Server, ServerWebSocket } from "bun";
import type { Verify } from "./auth";
import { type Ledger, type Message, ROOM } from "./history";
import type { PrivateEvent, RoomEvent } from "./module";
import { Presence } from "./presence";
import { RateLimiter } from "./ratelimit";
import { normalizeText, validateText } from "./text";

export const CLOSE_UNAUTHORIZED = 4401;
const AUTH_TIMEOUT_MS = 5_000;
const TYPING_INTERVAL_MS = 2_000;
const HISTORY_PAGE = 50;
const HISTORY_PAGE_MAX = 100;
const NONCE_TTL_MS = 10 * 60_000;
const NONCE_MAX_LENGTH = 64;
const SEARCH_LIMIT = 50;
const QUERY_MAX_LENGTH = 64;
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
  signedIn?: (handle: string) => void;
  /** Hands a private message to TNT for an SSH user; false when TNT cannot take one. */
  privatePost?: (from: string, to: string, text: string) => boolean;
  /** Tells TNT which app users are online, whenever that set changes. */
  remotePresence?: (handles: string[]) => void;
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
  const historyLimiter = new RateLimiter(10, 2);
  const clients = new Set<Socket>();
  const now = options.now ?? (() => new Date());
  const typingAt = new Map<string, number>();
  const sent = new Map<string, { message: Message; at: number }>();
  let online = "[]";
  let remote = "[]";

  const broadcast = (frame: object) => {
    const data = JSON.stringify(frame);
    for (const ws of clients) ws.send(data);
  };
  const deliver = (message: Message, nonce?: string) => broadcast(nonce ? { type: "message", ...message, nonce } : { type: "message", ...message });
  const syncRemote = () => {
    const list = presence.web();
    const key = JSON.stringify(list);
    if (key === remote) return;
    remote = key;
    options.remotePresence?.(list);
  };
  const toHandle = (handle: string, frame: object) => {
    const data = JSON.stringify(frame);
    for (const ws of clients) if (ws.data.handle === handle) ws.send(data);
  };
  const stamp = () => now().toISOString().replace(/\.\d{3}Z$/, "Z");
  const syncPresence = () => {
    syncRemote();
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
    options.signedIn?.(handle);
    presence.webJoined(handle, ws.data.id);
    syncPresence();
    ws.send(
      JSON.stringify({ type: "ready", room: ROOM, you: handle, history: options.ledger.recent(), presence: presence.online() }),
    );
    clients.add(ws);
  }

  function sendHistory(ws: Socket, frame: Record<string, unknown>) {
    if (typeof frame.before !== "string") return fail(ws, "invalid_frame");
    if (!historyLimiter.take(ws.data.handle)) return fail(ws, "rate_limited");
    const limit = Math.min(HISTORY_PAGE_MAX, Math.max(1, Math.trunc(Number(frame.limit)) || HISTORY_PAGE));
    const page = options.ledger.before(frame.before, limit);
    if (page === null) return fail(ws, "unknown_message");
    ws.send(JSON.stringify({ type: "history", before: frame.before, ...page }));
  }

  function forwardTyping(ws: Socket) {
    const handle = ws.data.handle;
    const at = Date.now();
    if (at - (typingAt.get(handle) ?? 0) < TYPING_INTERVAL_MS) return;
    typingAt.set(handle, at);
    const data = JSON.stringify({ type: "typing", handle });
    for (const client of clients) if (client.data.handle !== handle) client.send(data);
  }

  function sendPrivate(ws: Socket, frame: Record<string, unknown>) {
    const text = validateText(frame.text);
    if (typeof frame.to !== "string" || frame.to === "") return fail(ws, "invalid_frame");
    if (text === null) return fail(ws, "invalid_text");
    const from = ws.data.handle;
    const to = frame.to;
    const toApp = presence.isWeb(to);
    if (!toApp && !presence.isSsh(to)) return fail(ws, "not_online");
    if (!limiter.take(from)) return fail(ws, "rate_limited");
    if (!toApp && !options.privatePost?.(from, to, text)) return fail(ws, "not_online");
    const message = { type: "private", id: crypto.randomUUID(), from, to, text, timestamp: stamp(), verified: true };
    if (toApp && to !== from) toHandle(to, message);
    const nonce = typeof frame.nonce === "string" && frame.nonce.length <= NONCE_MAX_LENGTH ? frame.nonce : undefined;
    toHandle(from, nonce ? { ...message, nonce } : message);
  }

  function search(ws: Socket, frame: Record<string, unknown>) {
    const query = typeof frame.query === "string" ? frame.query.trim() : "";
    if (query === "" || query.length > QUERY_MAX_LENGTH) return fail(ws, "invalid_frame");
    if (!historyLimiter.take(ws.data.handle)) return fail(ws, "rate_limited");
    ws.send(JSON.stringify({ type: "search", query, messages: options.ledger.search(query, SEARCH_LIMIT) }));
  }

  function receive(ws: Socket, frame: Frame) {
    if (frame?.type === "ping") return ws.send('{"type":"pong"}');
    if (frame?.type === "history") return sendHistory(ws, frame);
    if (frame?.type === "typing") return forwardTyping(ws);
    if (frame?.type === "private") return sendPrivate(ws, frame);
    if (frame?.type === "search") return search(ws, frame);
    if (frame?.type !== "send") return fail(ws, "invalid_frame");
    const text = validateText(frame.text);
    if (text === null) return fail(ws, "invalid_text");
    const sender = ws.data.handle;
    const nonce = typeof frame.nonce === "string" && frame.nonce.length <= NONCE_MAX_LENGTH ? frame.nonce : undefined;
    const key = nonce === undefined ? undefined : `${sender}\n${nonce}`;
    const earlier = key === undefined ? undefined : sent.get(key);
    if (earlier) return ws.send(JSON.stringify({ type: "message", ...earlier.message, nonce }));
    if (!limiter.take(sender)) return fail(ws, "rate_limited");
    options.post(sender, text);
    const message = options.ledger.add({ timestamp: stamp(), sender, text, verified: true });
    if (key !== undefined) sent.set(key, { message, at: Date.now() });
    deliver(message, nonce);
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
      historyLimiter.sweep(at);
      for (const [key, entry] of sent) if ((at ?? Date.now()) - entry.at > NONCE_TTL_MS) sent.delete(key);
      for (const [handle, time] of typingAt) if ((at ?? Date.now()) - time > TYPING_INTERVAL_MS) typingAt.delete(handle);
    },
    handlePrivate(event: PrivateEvent) {
      const to = [...clients].find((ws) => ws.data.handle.toLowerCase() === event.to.toLowerCase())?.data.handle;
      if (!to) return;
      toHandle(to, {
        type: "private",
        id: crypto.randomUUID(),
        from: event.from,
        to,
        text: normalizeText(event.plain_text),
        timestamp: event.timestamp,
        verified: event.verified,
      });
    },
    handleEvent(event: RoomEvent) {
      if (event.type === "message.created") {
        const { timestamp, sender, plain_text, verified } = event.message;
        deliver(options.ledger.add({ timestamp, sender, text: normalizeText(plain_text), verified }));
        return;
      }
      if (event.type === "presence.snapshot") presence.sshSnapshot(event.nicknames);
      else if (event.type === "presence.joined") presence.sshJoined(event.nickname);
      else presence.sshLeft(event.nickname);
      syncPresence();
    },
  };
}
