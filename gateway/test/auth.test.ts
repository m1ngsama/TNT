import { beforeAll, describe, expect, test } from "bun:test";
import { type JWTVerifyGetKey, SignJWT, createLocalJWKSet, exportJWK, generateKeyPair } from "jose";
import { createVerifier } from "../src/auth";

const ISSUER = "https://auth.m1ng.space";
let privateKey: CryptoKey;
let otherPrivateKey: CryptoKey;
let keys: JWTVerifyGetKey;

beforeAll(async () => {
  const pair = await generateKeyPair("EdDSA");
  privateKey = pair.privateKey;
  otherPrivateKey = (await generateKeyPair("EdDSA")).privateKey;
  keys = createLocalJWKSet({ keys: [{ ...(await exportJWK(pair.publicKey)), kid: "test", alg: "EdDSA" }] });
});

type SignOptions = { issuer?: string; audience?: string[]; expires?: number; key?: CryptoKey; kid?: string; alg?: string };

function sign(claims: Record<string, unknown>, options: SignOptions = {}): Promise<string> {
  return new SignJWT({ handle: "alice", token_use: "access", ...claims })
    .setProtectedHeader({ alg: options.alg ?? "EdDSA", kid: options.kid ?? "test" })
    .setIssuer(options.issuer ?? ISSUER)
    .setAudience(options.audience ?? ["vibecloud", "chan", "tnt"])
    .setSubject("user-1")
    .setIssuedAt()
    .setExpirationTime(options.expires ?? Math.floor(Date.now() / 1000) + 3600)
    .sign(options.key ?? privateKey);
}

describe("createVerifier", () => {
  test("returns the handle of a valid access token", async () => {
    expect(await createVerifier(ISSUER, keys)(await sign({}))).toBe("alice");
  });

  test("rejects wrong issuer, audience, token use, expiry or handle", async () => {
    const verify = createVerifier(ISSUER, keys);
    const tokens = [
      await sign({}, { issuer: "https://evil.example" }),
      await sign({}, { audience: ["chan"] }),
      await sign({ token_use: "refresh" }),
      await sign({}, { expires: Math.floor(Date.now() / 1000) - 60 }),
      await sign({ handle: "system" }),
      await sign({ handle: 42 }),
      "not-a-jwt",
    ];
    for (const token of tokens) expect(await verify(token)).toBeNull();
  });

  test("rejects a forged or mismatched signature", async () => {
    const verify = createVerifier(ISSUER, keys);
    const tokens = [
      await sign({}, { key: otherPrivateKey }),
      await sign({}, { kid: "unknown" }),
      await new SignJWT({ handle: "alice", token_use: "access" })
        .setProtectedHeader({ alg: "HS256", kid: "test" })
        .setIssuer(ISSUER)
        .setAudience(["vibecloud", "chan", "tnt"])
        .setSubject("user-1")
        .setIssuedAt()
        .setExpirationTime(Math.floor(Date.now() / 1000) + 3600)
        .sign(new TextEncoder().encode("attacker-controlled-secret")),
    ];
    for (const token of tokens) expect(await verify(token)).toBeNull();
  });
});
