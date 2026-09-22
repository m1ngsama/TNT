import { expect, test } from "bun:test";
import { Presence } from "../src/presence";

test("online is the sorted union of SSH and web users", () => {
  const presence = new Presence();
  presence.sshSnapshot(["dave", "alice"]);
  presence.webJoined("alice", "c1");
  presence.webJoined("bob", "c2");
  expect(presence.online()).toEqual(["alice", "bob", "dave"]);
  presence.sshLeft("alice");
  expect(presence.online()).toEqual(["alice", "bob", "dave"]);
  presence.webLeft("alice", "c1");
  expect(presence.online()).toEqual(["bob", "dave"]);
});

test("a web user stays online until their last connection closes", () => {
  const presence = new Presence();
  presence.webJoined("bob", "c1");
  presence.webJoined("bob", "c2");
  presence.webLeft("bob", "c1");
  expect(presence.online()).toEqual(["bob"]);
  presence.webLeft("bob", "c2");
  expect(presence.online()).toEqual([]);
});

test("duplicate join and leave events for the same connection are idempotent", () => {
  const presence = new Presence();
  presence.webJoined("bob", "c1");
  presence.webJoined("bob", "c1");
  presence.webLeft("bob", "c1");
  expect(presence.online()).toEqual([]);
  presence.webLeft("bob", "c1");
  expect(presence.online()).toEqual([]);
});

test("two connections for one nickname: only one leaving keeps it online", () => {
  const presence = new Presence();
  presence.webJoined("bob", "c1");
  presence.webJoined("bob", "c2");
  presence.webLeft("bob", "c1");
  expect(presence.online()).toEqual(["bob"]);
  presence.webLeft("bob", "c1");
  expect(presence.online()).toEqual(["bob"]);
});

test("SSH records are idempotent set operations", () => {
  const presence = new Presence();
  presence.sshJoined("carol");
  presence.sshJoined("carol");
  presence.sshLeft("erin");
  expect(presence.online()).toEqual(["carol"]);
});
