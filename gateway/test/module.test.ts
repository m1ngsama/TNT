import { describe, expect, test } from "bun:test";
import { EVENT_OK, HANDSHAKE_OK, encodePost, parseTntLine, supportsIdentity, supportsPrivate } from "../src/module";

describe("parseTntLine", () => {
  test("answers the handshake", () => {
    const { reply, event } = parseTntLine('{"type":"handshake","protocol":"tnt.module.v1","server":{"name":"tnt","version":"1.4.0"}}');
    expect(reply).toBe(HANDSHAKE_OK);
    expect(JSON.parse(reply!)).toMatchObject({ type: "handshake.ok", protocol: "tnt.module.v1" });
    expect(event).toEqual({ type: "handshake", version: "1.4.0" });
  });

  test("knows which servers accept identity records", () => {
    expect(supportsIdentity("1.4.0")).toBe(false);
    expect(supportsIdentity("1.5.0")).toBe(true);
    expect(supportsIdentity("2.0.0")).toBe(true);
    expect(supportsIdentity("")).toBe(false);
  });

  test("acknowledges every message.created, even a malformed one", () => {
    expect(
      parseTntLine('{"type":"message.created","message":{"id":"local-1","timestamp":"2026-09-22T10:00:00Z","sender":"carol","kind":"text","plain_text":"hi","metadata":{}}}'),
    ).toEqual({
      reply: EVENT_OK,
      event: { type: "message.created", message: { timestamp: "2026-09-22T10:00:00Z", sender: "carol", plain_text: "hi", verified: false } },
    });
    expect(parseTntLine('{"type":"message.created","message":{"sender":1}}')).toEqual({ reply: EVENT_OK, event: null });
  });

  test("reads the verified flag and identity requests", () => {
    expect(
      parseTntLine('{"type":"message.created","message":{"timestamp":"2026-09-22T10:00:00Z","sender":"m1ngsama","plain_text":"hi","metadata":{"verified":true}}}').event,
    ).toMatchObject({ message: { verified: true } });
    expect(parseTntLine('{"type":"identity.requested","session":"s-3","nickname":"anon"}')).toEqual({
      reply: null,
      event: { type: "identity.requested", session: "s-3" },
    });
    expect(parseTntLine('{"type":"identity.requested"}')).toEqual({ reply: null, event: null });
  });

  test("parses private messages from TNT and knows which servers take them", () => {
    expect(
      parseTntLine('{"type":"private.created","from":"carol","to":"bob","plain_text":"hi","timestamp":"2026-09-22T10:00:00Z","verified":true}'),
    ).toEqual({
      reply: null,
      event: { type: "private.created", from: "carol", to: "bob", plain_text: "hi", timestamp: "2026-09-22T10:00:00Z", verified: true },
    });
    expect(parseTntLine('{"type":"private.created","from":"carol"}')).toEqual({ reply: null, event: null });
    expect(supportsPrivate("1.5.0")).toBe(false);
    expect(supportsPrivate("1.6.0")).toBe(true);
  });

  test("parses presence records without answering them", () => {
    expect(parseTntLine('{"type":"presence.snapshot","nicknames":["a",1,"b"]}')).toEqual({
      reply: null,
      event: { type: "presence.snapshot", nicknames: ["a", "b"] },
    });
    expect(parseTntLine('{"type":"presence.left","nickname":"carol","timestamp":"2026-09-22T10:00:00Z"}')).toEqual({
      reply: null,
      event: { type: "presence.left", nickname: "carol" },
    });
  });

  test("ignores garbage and unknown records", () => {
    for (const line of ["", "not json", "null", "[]", '{"type":"future.event"}']) {
      expect(parseTntLine(line)).toEqual({ reply: null, event: null });
    }
  });
});

test("encodePost writes one JSON line", () => {
  expect(encodePost("alice", 'a\n"b"')).toBe('{"type":"message.post","sender":"alice","plain_text":"a\\n\\"b\\""}\n');
  expect(EVENT_OK).toBe('{"type":"event.ok"}\n');
});
