import { type JWTVerifyGetKey, createRemoteJWKSet, jwtVerify } from "jose";
import { isValidNickname } from "./text";

export type Verify = (token: string) => Promise<string | null>;

export function createVerifier(issuer: string, keys: JWTVerifyGetKey): Verify {
  return async (token) => {
    try {
      const { payload } = await jwtVerify(token, keys, {
        issuer,
        audience: "tnt",
        requiredClaims: ["exp", "sub"],
      });
      const handle = payload.handle;
      return payload.token_use === "access" && typeof handle === "string" && isValidNickname(handle)
        ? handle
        : null;
    } catch {
      return null;
    }
  };
}

export function remoteKeys(url: string): JWTVerifyGetKey {
  return createRemoteJWKSet(new URL(url), { cooldownDuration: 30_000 });
}
