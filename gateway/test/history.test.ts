import { afterEach, describe, expect, test } from "bun:test";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { Ledger, decodeContent, messageId, parseRecord, readHistory } from "../src/history";

const HELLO = { timestamp: "2026-09-22T10:00:00Z", sender: "alice", text: "hello" };

describe("parseRecord", () => {
  test("parses a v2 record and decodes its escapes", () => {
    expect(parseRecord("2026-09-22T10:00:00Z|alice|a\\\\b\\nc")).toEqual({
      timestamp: "2026-09-22T10:00:00Z",
      sender: "alice",
      text: "a\\b\nc",
    });
  });

  test("rejects lines that are not valid records", () => {
    for (const line of [
      "#tnt-message-log v2",
      "2026-09-22T10:00:00Z|alice",
      "2026-09-22T10:00:00Z|alice|a|b",
      "2026-09-22 10:00:00|alice|hi",
      "2026-09-22T10:00:00Z||hi",
      "2026-09-22T10:00:00Z|alice|",
      "2026-09-22T10:00:00Z|alice|bad\\t",
      "2026-09-22T10:00:00Z|alice|x\u0001",
    ]) {
      expect(parseRecord(line)).toBeNull();
    }
  });

  test("rejects a trailing backslash", () => {
    expect(decodeContent("a\\")).toBeNull();
  });
});

describe("messageId", () => {
  test("is the first 64 bits of a SHA-256 hex digest", () => {
    expect(messageId(HELLO, 0)).toBe("72ea8549f3ca76e9");
    expect(messageId(HELLO, 1)).toBe("aa2df149c78d997a");
  });
});

describe("Ledger", () => {
  test("numbers identical records by occurrence and keeps the newest", () => {
    const ledger = new Ledger(2);
    expect(ledger.add(HELLO).id).toBe("72ea8549f3ca76e9");
    expect(ledger.add(HELLO).id).toBe("aa2df149c78d997a");
    ledger.add({ ...HELLO, text: "later" });
    expect(ledger.recent()).toEqual([
      { id: "aa2df149c78d997a", room: "lobby", sender: "alice", text: "hello", timestamp: "2026-09-22T10:00:00Z" },
      { id: messageId({ ...HELLO, text: "later" }, 0), room: "lobby", sender: "alice", text: "later", timestamp: "2026-09-22T10:00:00Z" },
    ]);
  });
});

describe("readHistory", () => {
  let dir = "";
  afterEach(() => rmSync(dir, { recursive: true, force: true }));

  test("merges the rotated log, drops notices and module output, keeps the newest", async () => {
    dir = mkdtempSync(join(tmpdir(), "tnt-gateway-history-"));
    writeFileSync(join(dir, "messages.log.1"), "#tnt-message-log v2\n2026-09-22T09:00:00Z|alice|old\n");
    writeFileSync(
      join(dir, "messages.log"),
      [
        "#tnt-message-log v2",
        "2026-09-22T10:00:00Z|system|alice joined",
        "2026-09-22T10:00:01Z|module:echo|echo: hi",
        "2026-09-22T10:00:02Z|bob|new",
        "2026-09-22T10:00:03Z|carol|unterminated",
      ].join("\n"),
    );
    expect(await readHistory(dir, 10)).toEqual([
      { timestamp: "2026-09-22T09:00:00Z", sender: "alice", text: "old" },
      { timestamp: "2026-09-22T10:00:02Z", sender: "bob", text: "new" },
    ]);
    expect(await readHistory(dir, 1)).toEqual([{ timestamp: "2026-09-22T10:00:02Z", sender: "bob", text: "new" }]);
  });

  test("returns nothing when no log exists", async () => {
    dir = mkdtempSync(join(tmpdir(), "tnt-gateway-history-"));
    expect(await readHistory(dir, 10)).toEqual([]);
  });
});
