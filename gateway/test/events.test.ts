import { afterEach, expect, test } from "bun:test";
import { mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { createReporter } from "../src/events";

let dir = "";
afterEach(() => rmSync(dir, { recursive: true, force: true }));

function setup(eventsStatus: () => number) {
  dir = mkdtempSync(join(tmpdir(), "tnt-events-"));
  const posted: unknown[] = [];
  const lookups: string[] = [];
  const fetch = (async (url: string, init?: RequestInit) => {
    const path = new URL(url).pathname;
    if (path.startsWith("/people/")) {
      lookups.push(path);
      return path === "/people/lune" ? Response.json({ sub: "sub-lune" }) : Response.json({ error: "not_found" }, { status: 404 });
    }
    posted.push({ authorization: new Headers(init?.headers).get("authorization"), body: JSON.parse(String(init?.body)) });
    return new Response(null, { status: eventsStatus() });
  }) as unknown as typeof globalThis.fetch;
  const options = { issuer: "https://id.test", secret: "s3cret", file: join(dir, "events.json"), fetch };
  return { reporter: createReporter(options), options, posted, lookups };
}

test("reports an event with the person's sub and forgets it once accepted", async () => {
  const { reporter, posted, lookups } = setup(() => 202);
  await reporter.report({ handle: "lune", type: "tnt.key_recognized", detail: { fingerprint: "SHA256:abc" } });
  await reporter.flush();
  await reporter.report({ handle: "lune", type: "tnt.signed_out", detail: { forgot_key: false } });
  await reporter.flush();
  expect(lookups).toEqual(["/people/lune"]);
  expect(posted[0]).toMatchObject({
    authorization: `Basic ${btoa("tnt:s3cret")}`,
    body: { events: [{ sub: "sub-lune", type: "tnt.key_recognized", actor: { kind: "self" }, detail: { fingerprint: "SHA256:abc" } }] },
  });
  expect(reporter.pending()).toEqual([]);
});

test("keeps refused events on disk and sends them after a restart", async () => {
  let status = 503;
  const { reporter, options, posted } = setup(() => status);
  await reporter.report({ handle: "lune", type: "tnt.signed_in", detail: { method: "device_code" } });
  await reporter.flush();
  expect(reporter.pending()).toHaveLength(1);
  const id = reporter.pending()[0]!.id;

  status = 202;
  const restarted = createReporter(options);
  await restarted.load();
  await restarted.flush();
  expect(restarted.pending()).toEqual([]);
  expect((posted.at(-1) as { body: { events: { id: string }[] } }).body.events[0]!.id).toBe(id);
});

test("drops events for a handle m1ngsama ID does not know", async () => {
  const { reporter, posted } = setup(() => 202);
  await reporter.report({ handle: "ghost", type: "tnt.signed_out" });
  await reporter.flush();
  expect(posted).toEqual([]);
  expect(reporter.pending()).toEqual([]);
});
