import type { Verify } from "./auth";

const CLIENT_ID = "tnt";
const DEVICE_GRANT = "urn:ietf:params:oauth:grant-type:device_code";

export type SignInOptions = {
  issuer: string;
  verify: Verify;
  notice: (text: string) => void;
  verified: (handle: string) => void;
  fetch?: typeof fetch;
  sleep?: (ms: number) => Promise<void>;
  now?: () => number;
};

type Grant = { device_code: string; user_code: string; verification_uri_complete: string; expires_in: number; interval: number };

const UNAVAILABLE = "Sign-in is unavailable now. 登录暂时不可用，请稍后再试。";

/** Runs an RFC 8628 device authorization grant against m1ngsama ID for one terminal session. */
export async function signIn(options: SignInOptions): Promise<void> {
  const request = options.fetch ?? fetch;
  const sleep = options.sleep ?? ((ms: number) => Bun.sleep(ms));
  const now = options.now ?? Date.now;
  const form = (fields: Record<string, string>) => ({ method: "POST", body: new URLSearchParams(fields) });
  try {
    const started = await request(`${options.issuer}/device/code`, form({ client_id: CLIENT_ID }));
    if (!started.ok) return options.notice(UNAVAILABLE);
    const grant = (await started.json()) as Grant;
    const minutes = Math.round(grant.expires_in / 60);
    // TNT cuts a notice at the terminal width, so each line goes on its own.
    options.notice("Open this link to sign in / 在浏览器打开链接登录:");
    options.notice(grant.verification_uri_complete);
    options.notice(`Code / 验证码 ${grant.user_code}, ${minutes} min`);
    const deadline = now() + grant.expires_in * 1000;
    let interval = grant.interval * 1000;
    while (now() < deadline) {
      await sleep(interval);
      const polled = await request(
        `${options.issuer}/device/token`,
        form({ grant_type: DEVICE_GRANT, device_code: grant.device_code, client_id: CLIENT_ID }),
      );
      const body = (await polled.json()) as { access_token?: string; error?: string };
      if (polled.ok && body.access_token) {
        const handle = await options.verify(body.access_token);
        return handle ? options.verified(handle) : options.notice(UNAVAILABLE);
      }
      if (body.error === "authorization_pending") continue;
      if (body.error === "slow_down") {
        interval += 5000;
        continue;
      }
      if (body.error === "access_denied") return options.notice("Sign-in was cancelled. 已取消登录。");
      break;
    }
    options.notice("The code expired. Run :login again. 验证码已过期。");
  } catch {
    options.notice(UNAVAILABLE);
  }
}
