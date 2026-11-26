import { afterEach, beforeEach, describe, expect, test } from "bun:test";
import { createGateway } from "../src/gateway";
import { Ledger } from "../src/history";
import { connect } from "./client";

let server: ReturnType<typeof Bun.serve>;
let posts: [string, string, string][];
let roomsOn: boolean;
let gateway: ReturnType<typeof createGateway>;
let url: string;

beforeEach(() => {
  posts = [];
  roomsOn = true;
  const ledger = new Ledger(100);
  ledger.add({ timestamp: "2026-09-22T09:59:00Z", sender: "carol", text: "earlier" });
  gateway = createGateway({
    ledger,
    verify: async (token) => (token === "alice" || token === "bob" ? token : null),
    post: (sender, text, room) => posts.push([sender, text, room]),
    roomsEnabled: () => roomsOn,
    now: () => new Date("2026-09-22T10:00:00.900Z"),
    authTimeoutMs: 200,
  });
  server = Bun.serve({ hostname: "127.0.0.1", port: 0, fetch: gateway.fetch, websocket: gateway.websocket });
  url = `ws://127.0.0.1:${server.port}/ws`;
});

afterEach(() => {
  void server.stop(true);
});

describe("auth", () => {
  test("closes with 4401 when no auth frame arrives in time", async () => {
    expect(await connect(url).closed).toBe(4401);
  });

  test("closes with 4401 on a rejected token", async () => {
    expect(await connect(url, "mallory").closed).toBe(4401);
  });

  test("answers other paths with 404", async () => {
    expect((await fetch(`http://127.0.0.1:${server.port}/`)).status).toBe(404);
  });
});

describe("session", () => {
  test("ready carries history and presence", async () => {
    gateway.handleEvent({ type: "presence.snapshot", nicknames: ["dave"] });
    const alice = connect(url, "alice");
    expect(await alice.next((f) => f.type === "ready")).toEqual({
      type: "ready",
      room: "lobby",
      you: "alice",
      history: [expect.objectContaining({ room: "lobby", sender: "carol", text: "earlier" })],
      presence: ["alice", "dave"],
    });
  });

  test("send posts to TNT and echoes a message with a stable id", async () => {
    const alice = connect(url, "alice");
    await alice.next((f) => f.type === "ready");
    alice.send({ type: "send", text: "hello" });
    expect(await alice.next((f) => f.type === "message")).toEqual({
      type: "message",
      id: "72ea8549f3ca76e9",
      room: "lobby",
      sender: "alice",
      text: "hello",
      timestamp: "2026-09-22T10:00:00Z",
      kind: "text",
      mentions: [],
      verified: true,
    });
    expect(posts).toEqual([["alice", "hello", "lobby"]]);
  });

  test("rejects invalid text and enforces the rate limit", async () => {
    const alice = connect(url, "alice");
    await alice.next((f) => f.type === "ready");
    alice.send({ type: "send", text: "\u001b[2J" });
    expect(await alice.next((f) => f.type === "error")).toEqual({ type: "error", code: "invalid_text" });
    for (const text of ["1", "2", "3", "4"]) alice.send({ type: "send", text });
    expect(await alice.next((f) => f.type === "error")).toEqual({ type: "error", code: "rate_limited" });
    expect(posts.map(([, text]) => text)).toEqual(["1", "2", "3"]);
  });

  test("answers ping and rejects unknown frames", async () => {
    const alice = connect(url, "alice");
    await alice.next((f) => f.type === "ready");
    alice.send({ type: "ping" });
    expect(await alice.next((f) => f.type === "pong")).toEqual({ type: "pong" });
    alice.send({ type: "dance" });
    expect(await alice.next((f) => f.type === "error")).toEqual({ type: "error", code: "invalid_frame" });
  });

  test("broadcasts TNT messages and presence changes", async () => {
    const alice = connect(url, "alice");
    await alice.next((f) => f.type === "ready");
    const bob = connect(url, "bob");
    expect(await alice.next((f) => f.type === "presence")).toEqual({ type: "presence", room: "lobby", online: ["alice", "bob"] });
    await bob.next((f) => f.type === "ready");

    gateway.handleEvent({ type: "presence.joined", nickname: "carol" });
    expect((await bob.next((f) => f.type === "presence")).online).toEqual(["alice", "bob", "carol"]);

    gateway.handleEvent({
      type: "message.created",
      message: { timestamp: "2026-09-22T10:00:05Z", sender: "carol", plain_text: "a|b", verified: false },
    });
    expect(await alice.next((f) => f.type === "message")).toMatchObject({
      sender: "carol",
      text: "a b",
      timestamp: "2026-09-22T10:00:05Z",
    });

    bob.ws.close();
    expect((await alice.next((f) => f.type === "presence" && !f.online.includes("bob"))).online).toEqual(["alice", "carol"]);
  });
});

test("a second connection for the same user keeps them online when one closes", async () => {
  const first = connect(url, "alice");
  await first.next((f) => f.type === "ready");
  const second = connect(url, "alice");
  await second.next((f) => f.type === "ready");
  const bob = connect(url, "bob");
  await bob.next((f) => f.type === "ready");
  first.ws.close();
  await first.closed;
  bob.send({ type: "ping" });
  await bob.next((f) => f.type === "pong");
  await expect(bob.next((f) => f.type === "presence" && !f.online.includes("alice"), 300)).rejects.toThrow();
});

describe("idempotent sends", () => {
  test("a resend with the same nonce echoes the first message and posts once", async () => {
    const alice = connect(url, "alice");
    await alice.next((f) => f.type === "ready");
    alice.send({ type: "send", text: "hello", nonce: "n1" });
    const first = await alice.next((f) => f.type === "message");
    expect(first.nonce).toBe("n1");
    alice.send({ type: "send", text: "hello", nonce: "n1" });
    const again = await alice.next((f) => f.type === "message");
    expect(again.id).toBe(first.id);
    expect(posts).toEqual([["alice", "hello", "lobby"]]);
    alice.send({ type: "send", text: "hello" });
    expect((await alice.next((f) => f.type === "message")).id).not.toBe(first.id);
  });

  test("nonces are private to their sender", async () => {
    const alice = connect(url, "alice");
    await alice.next((f) => f.type === "ready");
    const bob = connect(url, "bob");
    await bob.next((f) => f.type === "ready");
    alice.send({ type: "send", text: "hi", nonce: "shared" });
    const a = await alice.next((f) => f.type === "message");
    bob.send({ type: "send", text: "hi", nonce: "shared" });
    const b = await bob.next((f) => f.type === "message" && f.sender === "bob");
    expect(b.id).not.toBe(a.id);
    expect(posts.length).toBe(2);
  });
});

describe("message kinds", () => {
  test("classifies actions, notices and module replies and lists mentions", async () => {
    const alice = connect(url, "alice");
    await alice.next((f) => f.type === "ready");
    gateway.handleEvent({ type: "message.created", message: { timestamp: "2026-09-22T10:00:01Z", sender: "*", plain_text: "carol waves at @alice", verified: false } });
    expect(await alice.next((f) => f.type === "message")).toMatchObject({ kind: "action", sender: "carol", text: "waves at @alice", mentions: ["alice"] });
    gateway.handleEvent({ type: "message.created", message: { timestamp: "2026-09-22T10:00:02Z", sender: "系统", plain_text: "carol 加入了聊天室", verified: false } });
    expect(await alice.next((f) => f.type === "message")).toMatchObject({ kind: "system", sender: "系统", text: "carol 加入了聊天室" });
    gateway.handleEvent({ type: "message.created", message: { timestamp: "2026-09-22T10:00:03Z", sender: "module:roll-module", plain_text: "🎲 carol rolled d6 → 4", verified: false } });
    expect(await alice.next((f) => f.type === "message")).toMatchObject({ kind: "module", sender: "roll-module", text: "🎲 carol rolled d6 → 4" });
    alice.send({ type: "send", text: "hi @Bob, @nobody" });
    expect(await alice.next((f) => f.type === "message")).toMatchObject({ kind: "text", sender: "alice", mentions: ["bob", "nobody"] });
  });

  test("history keeps notices and module replies", async () => {
    gateway.handleEvent({ type: "message.created", message: { timestamp: "2026-09-22T10:00:04Z", sender: "module:flip-module", plain_text: "🪙 carol flipped → heads", verified: false } });
    const alice = connect(url, "alice");
    const ready = await alice.next((f) => f.type === "ready");
    expect(ready.history.at(-1)).toMatchObject({ kind: "module", sender: "flip-module" });
  });
});

describe("history and typing", () => {
  test("answers history pages and rejects unknown ids", async () => {
    const alice = connect(url, "alice");
    const ready = await alice.next((f) => f.type === "ready");
    const first = ready.history[0].id;
    alice.send({ type: "history", before: first, limit: "50" });
    expect(await alice.next((f) => f.type === "history")).toEqual({ type: "history", before: first, messages: [], more: false });
    alice.send({ type: "history", before: "nope", limit: 50 });
    expect(await alice.next((f) => f.type === "error")).toEqual({ type: "error", code: "unknown_message" });
  });

  test("reaches back to a message the client asks for", async () => {
    const alice = connect(url, "alice");
    const ready = await alice.next((f) => f.type === "ready");
    for (let i = 0; i < 3; i++) alice.send({ type: "send", text: `m${i}`, nonce: `n${i}` });
    const last = await alice.next((f) => f.type === "message" && f.nonce === "n2");
    alice.send({ type: "history", before: last.id, until: ready.history[0].id });
    const page = await alice.next((f) => f.type === "history");
    expect(page.messages.map((m: { text: string }) => m.text)).toEqual(["earlier", "m0", "m1"]);
    expect(page.more).toBe(false);
    alice.send({ type: "history", before: last.id, until: "nope" });
    expect(await alice.next((f) => f.type === "error")).toEqual({ type: "error", code: "unknown_message" });
  });

  test("limits history requests", async () => {
    const alice = connect(url, "alice");
    const ready = await alice.next((f) => f.type === "ready");
    for (let i = 0; i < 11; i++) alice.send({ type: "history", before: ready.history[0].id, limit: 1 });
    expect(await alice.next((f) => f.type === "error")).toEqual({ type: "error", code: "rate_limited" });
  });

  test("forwards typing to others at most every two seconds", async () => {
    const alice = connect(url, "alice");
    await alice.next((f) => f.type === "ready");
    const bob = connect(url, "bob");
    await bob.next((f) => f.type === "ready");
    alice.send({ type: "typing" });
    expect(await bob.next((f) => f.type === "typing")).toEqual({ type: "typing", handle: "alice" });
    alice.send({ type: "typing" });
    await expect(bob.next((f) => f.type === "typing", 300)).rejects.toThrow();
    await expect(alice.next((f) => f.type === "typing", 300)).rejects.toThrow();
  });
});

describe("private messages", () => {
  let delivered: [string, string, string][];
  let remote: string[][];
  let direct: ReturnType<typeof createGateway>;
  let directServer: ReturnType<typeof Bun.serve>;
  let directUrl: string;

  beforeEach(() => {
    delivered = [];
    remote = [];
    direct = createGateway({
      ledger: new Ledger(100),
      verify: async (token) => (["alice", "bob"].includes(token) ? token : null),
      post: () => {},
      privatePost: (from, to, text) => {
        delivered.push([from, to, text]);
        return true;
      },
      remotePresence: (handles) => remote.push(handles),
      now: () => new Date("2026-09-22T10:00:00.900Z"),
    });
    directServer = Bun.serve({ hostname: "127.0.0.1", port: 0, fetch: direct.fetch, websocket: direct.websocket });
    directUrl = `ws://127.0.0.1:${directServer.port}/ws`;
  });

  afterEach(() => {
    void directServer.stop(true);
  });

  test("tells TNT which app users are online", async () => {
    const alice = connect(directUrl, "alice");
    await alice.next((f) => f.type === "ready");
    const bob = connect(directUrl, "bob");
    await bob.next((f) => f.type === "ready");
    bob.ws.close();
    await bob.closed;
    await alice.next((f) => f.type === "presence" && f.online.length === 1);
    expect(remote).toEqual([["alice"], ["alice", "bob"], ["alice"]]);
  });

  test("delivers between app users and echoes to the sender", async () => {
    const alice = connect(directUrl, "alice");
    const bob = connect(directUrl, "bob");
    await alice.next((f) => f.type === "ready");
    await bob.next((f) => f.type === "ready");
    alice.send({ type: "private", to: "bob", text: "psst", nonce: "n1" });
    expect(await bob.next((f) => f.type === "private")).toMatchObject({ from: "alice", to: "bob", text: "psst", verified: true });
    expect(await alice.next((f) => f.type === "private")).toMatchObject({ from: "alice", to: "bob", nonce: "n1" });
    expect(delivered).toEqual([]);
  });

  test("hands messages for SSH users to TNT and refuses names nobody uses", async () => {
    direct.handleEvent({ type: "presence.snapshot", nicknames: ["Carol"] });
    const alice = connect(directUrl, "alice");
    await alice.next((f) => f.type === "ready");
    alice.send({ type: "private", to: "carol", text: "hi carol" });
    expect(await alice.next((f) => f.type === "private")).toMatchObject({ to: "carol", text: "hi carol" });
    expect(delivered).toEqual([["alice", "carol", "hi carol"]]);
    alice.send({ type: "private", to: "nobody", text: "hello?" });
    expect(await alice.next((f) => f.type === "error")).toEqual({ type: "error", code: "not_online" });
  });

  test("passes TNT's private messages to the app user they name", async () => {
    const bob = connect(directUrl, "bob");
    await bob.next((f) => f.type === "ready");
    direct.handlePrivate({ type: "private.created", from: "carol", to: "Bob", plain_text: "back at you", timestamp: "2026-09-22T10:00:00Z", verified: false });
    expect(await bob.next((f) => f.type === "private")).toMatchObject({ from: "carol", to: "bob", text: "back at you", verified: false });
  });
});

describe("rooms", () => {
  const event = (text: string, room?: string) =>
    gateway.handleEvent({ type: "message.created", ...(room ? { room } : {}), message: { timestamp: "2026-09-22T10:00:05Z", sender: "carol", plain_text: text, verified: false } });

  test("a client that never sends a room frame stays in the lobby", async () => {
    const alice = connect(url, "alice");
    expect(await alice.next((f) => f.type === "ready")).toMatchObject({ room: "lobby" });
    event("in ops", "ops");
    event("in lobby");
    expect(await alice.next((f) => f.type === "message")).toMatchObject({ room: "lobby", text: "in lobby" });
    alice.send({ type: "send", text: "hello" });
    expect(await alice.next((f) => f.type === "message")).toMatchObject({ room: "lobby", text: "hello" });
    expect(posts).toEqual([["alice", "hello", "lobby"]]);
    alice.send({ type: "search", query: "in" });
    expect((await alice.next((f) => f.type === "search")).messages.map((m: { text: string }) => m.text)).toEqual(["in lobby"]);
  });

  test("a client that joins a room sees that room and not the lobby", async () => {
    event("ops before", "ops");
    const bob = connect(url, "bob");
    await bob.next((f) => f.type === "ready");
    bob.send({ type: "join", room: "ops" });
    expect(await bob.next((f) => f.type === "joined")).toMatchObject({
      room: "ops",
      history: [expect.objectContaining({ room: "ops", text: "ops before" })],
      presence: ["bob"],
    });
    event("lobby chatter");
    event("ops news", "ops");
    expect(await bob.next((f) => f.type === "message")).toMatchObject({ room: "ops", text: "ops news" });
    await expect(bob.next((f) => f.type === "message" && f.room === "lobby", 300)).rejects.toThrow();
    bob.send({ type: "send", text: "deploying" });
    expect(await bob.next((f) => f.type === "message")).toMatchObject({ room: "ops", text: "deploying" });
    expect(posts).toEqual([["bob", "deploying", "ops"]]);
  });

  test("lobby presence lists everyone and a room lists its members", async () => {
    const alice = connect(url, "alice");
    await alice.next((f) => f.type === "ready");
    const bob = connect(url, "bob");
    await bob.next((f) => f.type === "ready");
    bob.send({ type: "join", room: "ops" });
    await bob.next((f) => f.type === "joined");
    gateway.handleEvent({ type: "presence.joined", room: "ops", nickname: "dave" });
    expect((await bob.next((f) => f.type === "presence" && f.online.includes("dave"))).online).toEqual(["bob", "dave"]);
    bob.send({ type: "join", room: "lobby" });
    expect(await bob.next((f) => f.type === "joined")).toMatchObject({ room: "lobby", presence: ["alice", "bob"] });
  });

  test("refuses bad names and rooms TNT has not agreed to", async () => {
    const alice = connect(url, "alice");
    await alice.next((f) => f.type === "ready");
    alice.send({ type: "join", room: "Ops" });
    expect(await alice.next((f) => f.type === "error")).toEqual({ type: "error", code: "invalid_frame" });
    roomsOn = false;
    alice.send({ type: "join", room: "ops" });
    expect(await alice.next((f) => f.type === "error")).toEqual({ type: "error", code: "rooms_unavailable" });
  });
});

describe("search", () => {
  test("finds senders and text, newest first, without notices", async () => {
    gateway.handleEvent({ type: "message.created", message: { timestamp: "2026-09-22T10:00:01Z", sender: "system", plain_text: "Release joined", verified: false } });
    gateway.handleEvent({ type: "message.created", message: { timestamp: "2026-09-22T10:00:02Z", sender: "dave", plain_text: "the release is out", verified: false } });
    gateway.handleEvent({ type: "message.created", message: { timestamp: "2026-09-22T10:00:03Z", sender: "release_bot", plain_text: "ok", verified: false } });
    const alice = connect(url, "alice");
    await alice.next((f) => f.type === "ready");
    alice.send({ type: "search", query: " RELEASE " });
    const result = await alice.next((f) => f.type === "search");
    expect(result.query).toBe("RELEASE");
    expect(result.messages.map((m: { sender: string }) => m.sender)).toEqual(["release_bot", "dave"]);
    alice.send({ type: "search", query: "" });
    expect(await alice.next((f) => f.type === "error")).toEqual({ type: "error", code: "invalid_frame" });
  });
});
