export type Frame = Record<string, any>;

export function connect(url: string, token?: string) {
  const ws = new WebSocket(url);
  const frames: Frame[] = [];
  let wake = () => {};

  ws.addEventListener("message", (event) => {
    frames.push(JSON.parse(String(event.data)));
    wake();
  });
  if (token !== undefined) {
    ws.addEventListener("open", () => ws.send(JSON.stringify({ type: "auth", token })));
  }
  const closed = new Promise<number>((resolve) => ws.addEventListener("close", (event) => resolve(event.code)));

  async function next(match: (frame: Frame) => boolean, timeoutMs = 5_000): Promise<Frame> {
    const deadline = Date.now() + timeoutMs;
    for (;;) {
      const index = frames.findIndex(match);
      if (index >= 0) return frames.splice(index, 1)[0]!;
      if (Date.now() > deadline) throw new Error(`no matching frame; saw ${JSON.stringify(frames)}`);
      await new Promise<void>((resolve) => {
        wake = resolve;
        setTimeout(resolve, 50);
      });
    }
  }

  return { ws, closed, next, send: (frame: Frame) => ws.send(JSON.stringify(frame)) };
}
