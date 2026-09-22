import { describe, expect, test } from "bun:test";
import { isValidNickname, normalizeText, validateText } from "../src/text";

describe("isValidNickname", () => {
  test("accepts handles and TNT-style names", () => {
    for (const name of ["alice", "a_1", "abcdefghijklmnopqrst", "小明"]) {
      expect(isValidNickname(name)).toBe(true);
    }
  });

  test("rejects names TNT refuses", () => {
    for (const name of [
      "", "system", "系统", "*", "module:echo", "-a", ".a", " a", "a|b", "a;b",
      "a\u0001", "abcdefghijklmnopqrstu", "a:b", ":", "a:",
    ]) {
      expect(isValidNickname(name)).toBe(false);
    }
  });
});

describe("validateText", () => {
  test("accepts 1 to 1023 stored bytes", () => {
    expect(validateText("hi")).toBe("hi");
    expect(validateText("a".repeat(1023))).toBe("a".repeat(1023));
    expect(validateText("line\nnext")).toBe("line\nnext");
  });

  test("counts log escapes toward the limit", () => {
    expect(validateText(`${"a".repeat(1022)}\n`)).toBeNull();
    expect(validateText(`${"a".repeat(1021)}\\`)).toBe(`${"a".repeat(1021)}\\`);
  });

  test("rejects empty, control, oversize and malformed text", () => {
    for (const bad of ["", "   ", "\u001b[2J", "a\u0085", "a".repeat(1024), "\ud800", 42, null]) {
      expect(validateText(bad)).toBeNull();
    }
  });

  test("rewrites the characters TNT replaces in its log", () => {
    expect(validateText("a|b\r\nc")).toBe("a b \nc");
    expect(normalizeText("x|y")).toBe("x y");
  });
});
