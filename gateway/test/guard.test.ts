import { expect, test } from "bun:test";
import { createGuard } from "../src/guard";

function setup(answers: Record<string, string>) {
  const reserved: string[] = [];
  const asked: string[] = [];
  let clock = 0;
  const guard = createGuard({
    issuer: "https://id.test",
    reserve: (handle) => reserved.push(handle),
    fetch: (async (url: string) => {
      const name = new URL(url).searchParams.get("name")!;
      asked.push(name);
      const handle = answers[name];
      return handle ? Response.json({ handle }) : Response.json({ error: "not_found" }, { status: 404 });
    }) as unknown as typeof fetch,
    now: () => clock,
  });
  return { guard, reserved, asked, tick: (ms: number) => (clock += ms) };
}

test("reserves the member a nickname names or imitates, once", async () => {
  const { guard, reserved, asked } = setup({ m1ngsarna: "m1ngsama", m1ngsama: "m1ngsama" });
  await guard.check("m1ngsarna");
  await guard.check("m1ngsama");
  await guard.check("stranger");
  expect(asked).toEqual(["m1ngsarna", "m1ngsama", "stranger"]);
  expect(reserved).toEqual(["m1ngsama"]);
});

test("skips anonymous names and asks again only after ten minutes", async () => {
  const { guard, asked, tick } = setup({});
  await guard.check("anonymous");
  await guard.check("anonymous12");
  await guard.check("carol");
  await guard.check("Carol");
  tick(10 * 60_000);
  await guard.check("carol");
  expect(asked).toEqual(["carol", "carol"]);
});

test("a handle reserved for a signed-in app user is not sent twice", () => {
  const { guard, reserved } = setup({});
  guard.reserve("lune");
  guard.reserve("lune");
  expect(reserved).toEqual(["lune"]);
});

test("an unreachable identity provider leaves the name to be checked again", async () => {
  let calls = 0;
  const guard = createGuard({
    issuer: "https://id.test",
    reserve: () => {},
    fetch: (async () => {
      calls++;
      throw new Error("offline");
    }) as unknown as typeof fetch,
  });
  await guard.check("carol");
  await guard.check("carol");
  expect(calls).toBe(2);
});
