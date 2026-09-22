export class RateLimiter {
  readonly #burst: number;
  readonly #perSecond: number;
  #buckets = new Map<string, { tokens: number; at: number }>();

  constructor(burst: number, perSecond: number) {
    this.#burst = burst;
    this.#perSecond = perSecond;
  }

  take(key: string, now = Date.now()): boolean {
    const bucket = this.#buckets.get(key) ?? { tokens: this.#burst, at: now };
    bucket.tokens = Math.min(this.#burst, bucket.tokens + ((now - bucket.at) / 1000) * this.#perSecond);
    bucket.at = now;
    this.#buckets.set(key, bucket);
    if (bucket.tokens < 1) return false;
    bucket.tokens -= 1;
    return true;
  }
}
