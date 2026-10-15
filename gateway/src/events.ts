const FLUSH_MS = 30_000;
const BATCH = 50;
const MAX_PENDING = 1000;

export type IdentityEvent = {
  id: string;
  handle: string;
  type: `tnt.${string}`;
  at: number;
  detail?: Record<string, string | boolean>;
};

export type ReporterOptions = {
  issuer: string;
  secret: string;
  /** Pending events survive a restart here. */
  file: string;
  fetch?: typeof fetch;
};

/** Reports terminal identity events to m1ngsama ID, keeping them until it accepts them. */
export function createReporter(options: ReporterOptions) {
  const request = options.fetch ?? fetch;
  const subs = new Map<string, string>();
  let pending: IdentityEvent[] = [];
  let flushing: Promise<void> | null = null;

  async function load() {
    const file = Bun.file(options.file);
    if (!(await file.exists())) return;
    try {
      pending = JSON.parse(await file.text()) as IdentityEvent[];
    } catch {
      pending = [];
    }
  }

  const save = () => Bun.write(options.file, JSON.stringify(pending));

  async function subFor(handle: string): Promise<string | null> {
    const known = subs.get(handle);
    if (known) return known;
    const res = await request(`${options.issuer}/people/${encodeURIComponent(handle)}`);
    if (res.status === 404) return null;
    if (!res.ok) throw new Error(`people lookup answered ${res.status}`);
    const { sub } = (await res.json()) as { sub?: unknown };
    if (typeof sub !== "string") return null;
    subs.set(handle, sub);
    return sub;
  }

  async function send(): Promise<void> {
    while (pending.length > 0) {
      const batch = pending.slice(0, BATCH);
      const events = [];
      const unknown = new Set<string>();
      for (const event of batch) {
        const sub = await subFor(event.handle);
        if (sub === null) unknown.add(event.id);
        else events.push({ id: event.id, sub, type: event.type, at: event.at, actor: { kind: "self" }, detail: event.detail });
      }
      if (events.length > 0) {
        const res = await request(`${options.issuer}/internal/events`, {
          method: "POST",
          headers: { authorization: `Basic ${btoa(`tnt:${options.secret}`)}`, "content-type": "application/json" },
          body: JSON.stringify({ events }),
        });
        if (res.status !== 202) throw new Error(`events answered ${res.status}`);
      }
      // A handle with no person behind it has nobody to attribute the event to.
      pending = pending.slice(batch.length);
      await save();
      if (unknown.size > 0) console.error(`identity events: dropped ${unknown.size} for handles m1ngsama ID does not know`);
    }
  }

  function flush(): Promise<void> {
    flushing ??= send()
      .catch((error: unknown) => console.error(`identity events: ${pending.length} waiting, ${String(error)}`))
      .finally(() => {
        flushing = null;
      });
    return flushing;
  }

  async function report(event: Omit<IdentityEvent, "id" | "at">) {
    pending.push({ id: crypto.randomUUID(), at: Date.now(), ...event });
    if (pending.length > MAX_PENDING) pending = pending.slice(-MAX_PENDING);
    await save();
    void flush();
  }

  return { load, report, flush, start: () => setInterval(() => void flush(), FLUSH_MS), pending: () => pending };
}
