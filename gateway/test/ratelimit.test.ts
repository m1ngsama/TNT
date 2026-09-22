import { expect, test } from "bun:test";
import { RateLimiter } from "../src/ratelimit";

test("allows a burst, then one per second, per key", () => {
  const limiter = new RateLimiter(3, 1);
  expect([0, 0, 0, 0].map((t) => limiter.take("alice", t))).toEqual([true, true, true, false]);
  expect(limiter.take("bob", 0)).toBe(true);
  expect(limiter.take("alice", 999)).toBe(false);
  expect(limiter.take("alice", 1000)).toBe(true);
  expect(limiter.take("alice", 1000)).toBe(false);
  expect(limiter.take("alice", 10_000)).toBe(true);
});

test("a backward clock is clamped instead of creating a token debt", () => {
  const limiter = new RateLimiter(1, 1);
  expect(limiter.take("alice", 1000)).toBe(true);
  expect(limiter.take("alice", 0)).toBe(false);
  expect(limiter.take("alice", 1000)).toBe(true);
});

test("sweep evicts fully-refilled buckets without changing later behavior", () => {
  const limiter = new RateLimiter(2, 1);
  limiter.take("alice", 0);
  limiter.sweep(10_000);
  expect(limiter.take("alice", 10_000)).toBe(true);
  expect(limiter.take("alice", 10_000)).toBe(true);
  expect(limiter.take("alice", 10_000)).toBe(false);
});
