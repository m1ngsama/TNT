import { expect, test } from "bun:test";
import { Presence } from "../src/presence";

test("online is the sorted union of SSH and web users", () => {
  const presence = new Presence();
  presence.sshSnapshot(["dave", "alice"]);
  presence.webJoined("alice");
  presence.webJoined("bob");
  expect(presence.online()).toEqual(["alice", "bob", "dave"]);
  presence.sshLeft("alice");
  expect(presence.online()).toEqual(["alice", "bob", "dave"]);
  presence.webLeft("alice");
  expect(presence.online()).toEqual(["bob", "dave"]);
});

test("a web user stays online until the last connection closes", () => {
  const presence = new Presence();
  presence.webJoined("bob");
  presence.webJoined("bob");
  presence.webLeft("bob");
  expect(presence.online()).toEqual(["bob"]);
  presence.webLeft("bob");
  presence.webLeft("bob");
  expect(presence.online()).toEqual([]);
});

test("SSH records are idempotent set operations", () => {
  const presence = new Presence();
  presence.sshJoined("carol");
  presence.sshJoined("carol");
  presence.sshLeft("erin");
  expect(presence.online()).toEqual(["carol"]);
});
