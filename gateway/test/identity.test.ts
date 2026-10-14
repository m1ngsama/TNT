import { describe, expect, test } from "bun:test";
import { signIn } from "../src/identity";

const GRANT = {
  device_code: "dc",
  user_code: "BCDF-GHJK",
  verification_uri_complete: "https://id.test/device?code=BCDF-GHJK",
  expires_in: 600,
  interval: 5,
};

function run(tokens: { status: number; body: object }[], verify = async (t: string) => (t === "good" ? "lune" : null)) {
  const notices: string[] = [];
  const verified: string[] = [];
  const waits: number[] = [];
  const requests: string[] = [];
  let clock = 0;
  const fake = (async (url: string | URL | Request, init?: RequestInit) => {
    requests.push(`${url} ${String(init?.body)}`);
    if (String(url).endsWith("/device/code")) return Response.json(GRANT);
    const next = tokens.shift() ?? { status: 400, body: { error: "expired_token" } };
    return Response.json(next.body, { status: next.status });
  }) as typeof fetch;
  const done = signIn({
    issuer: "https://id.test",
    verify,
    notice: (text) => notices.push(text),
    verified: (handle) => verified.push(handle),
    fetch: fake,
    sleep: async (ms) => {
      waits.push(ms);
      clock += ms;
    },
    now: () => clock,
  });
  return done.then(() => ({ notices, verified, waits, requests }));
}

describe("signIn", () => {
  test("shows the link, waits out pending and slow_down, then verifies", async () => {
    const result = await run([
      { status: 400, body: { error: "authorization_pending" } },
      { status: 400, body: { error: "slow_down" } },
      { status: 200, body: { access_token: "good", token_type: "Bearer", expires_in: 300 } },
    ]);
    expect(result.notices.slice(0, 3)).toEqual([
      "Open this link to sign in / 在浏览器打开链接登录:",
      "https://id.test/device?code=BCDF-GHJK",
      "Code / 验证码 BCDF-GHJK, 10 min",
    ]);
    for (const line of result.notices) expect(line.length).toBeLessThanOrEqual(60);
    expect(result.waits).toEqual([5000, 5000, 10000]);
    expect(result.verified).toEqual(["lune"]);
    expect(result.requests[0]).toBe("https://id.test/device/code client_id=tnt");
    expect(result.requests[1]).toContain("grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code&device_code=dc");
  });

  test("reports a cancelled request", async () => {
    const result = await run([{ status: 400, body: { error: "access_denied" } }]);
    expect(result.verified).toEqual([]);
    expect(result.notices.at(-1)).toContain("cancelled");
  });

  test("gives up when the code expires", async () => {
    const result = await run(Array.from({ length: 200 }, () => ({ status: 400, body: { error: "authorization_pending" } })));
    expect(result.waits.reduce((a, b) => a + b, 0)).toBe(600_000);
    expect(result.notices.at(-1)).toContain("expired");
  });

  test("refuses a token that does not verify", async () => {
    const result = await run([{ status: 200, body: { access_token: "forged" } }]);
    expect(result.verified).toEqual([]);
    expect(result.notices.at(-1)).toContain("unavailable");
  });

  test("survives the identity provider being down", async () => {
    const notices: string[] = [];
    await signIn({
      issuer: "https://id.test",
      verify: async () => null,
      notice: (text) => notices.push(text),
      verified: () => {},
      fetch: (async () => {
        throw new Error("offline");
      }) as unknown as typeof fetch,
    });
    expect(notices).toHaveLength(1);
  });
});
