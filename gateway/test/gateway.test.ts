import { afterEach, beforeEach, describe, expect, test } from "bun:test";
import { createGateway } from "../src/gateway";
import { Ledger } from "../src/history";
import { connect } from "./client";

let server: ReturnType<typeof Bun.serve>;
let posts: [string, string][];
let gateway: ReturnType<typeof createGateway>;
let url: string;

beforeEach(() => {
  posts = [];
  const ledger = new Ledger(100);
  ledger.add({ timestamp: "2026-09-22T09:59:00Z", sender: "carol", text: "earlier" });
  gateway = createGateway({
    ledger,
    verify: async (token) => (token === "alice" || token === "bob" ? token : null),
    post: (sender, text) => posts.push([sender, text]),
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
    });
    expect(posts).toEqual([["alice", "hello"]]);
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
      message: { timestamp: "2026-09-22T10:00:05Z", sender: "carol", plain_text: "a|b" },
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
