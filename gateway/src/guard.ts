const RECHECK_MS = 10 * 60_000;
const MAX_IN_FLIGHT = 4;
const ANONYMOUS = /^anonymous\d*$/i;

export type GuardOptions = {
  issuer: string;
  /** Reserves a member's handle in TNT, which renames anyone unverified using it or a look-alike. */
  reserve: (handle: string) => void;
  fetch?: typeof fetch;
  now?: () => number;
};

/** Asks m1ngsama ID whether a terminal nickname is, or looks like, a member's handle. */
export function createGuard(options: GuardOptions) {
  const request = options.fetch ?? fetch;
  const now = options.now ?? Date.now;
  const checked = new Map<string, number>();
  const reserved = new Set<string>();
  let inFlight = 0;

  const reserve = (handle: string) => {
    if (reserved.has(handle)) return;
    reserved.add(handle);
    options.reserve(handle);
  };

  async function check(nickname: string): Promise<void> {
    const key = nickname.toLowerCase();
    if (ANONYMOUS.test(nickname) || now() - (checked.get(key) ?? -Infinity) < RECHECK_MS || inFlight >= MAX_IN_FLIGHT) return;
    checked.set(key, now());
    inFlight++;
    try {
      const response = await request(`${options.issuer}/people/similar?${new URLSearchParams({ name: nickname })}`);
      if (!response.ok) return;
      const { handle } = (await response.json()) as { handle?: unknown };
      if (typeof handle === "string") reserve(handle);
    } catch {
      checked.delete(key);
    } finally {
      inFlight--;
    }
  }

  return { check, reserve };
}
