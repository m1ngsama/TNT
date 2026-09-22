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
