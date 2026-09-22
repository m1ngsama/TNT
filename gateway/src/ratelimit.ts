export class RateLimiter {
  readonly #burst: number;
  readonly #perSecond: number;
  #buckets = new Map<string, { tokens: number; at: number }>();

  constructor(burst: number, perSecond: number) {
    this.#burst = burst;
    this.#perSecond = perSecond;
  }

  #refill(bucket: { tokens: number; at: number } | undefined, now: number): number {
    if (!bucket) return this.#burst;
    const elapsed = Math.max(0, now - bucket.at);
    return Math.min(this.#burst, bucket.tokens + (elapsed / 1000) * this.#perSecond);
  }

  take(key: string, now = Date.now()): boolean {
    const tokens = this.#refill(this.#buckets.get(key), now);
    if (tokens < 1) {
      this.#buckets.set(key, { tokens, at: now });
      return false;
    }
    const remaining = tokens - 1;
    if (remaining >= this.#burst) this.#buckets.delete(key);
    else this.#buckets.set(key, { tokens: remaining, at: now });
    return true;
  }

  sweep(now = Date.now()): void {
    for (const [key, bucket] of this.#buckets) {
      if (this.#refill(bucket, now) >= this.#burst) this.#buckets.delete(key);
    }
  }
}
