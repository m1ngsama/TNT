export type TntEvent =
  | { type: "message.created"; message: { timestamp: string; sender: string; plain_text: string } }
  | { type: "presence.snapshot"; nicknames: string[] }
  | { type: "presence.joined" | "presence.left"; nickname: string };

export const HANDSHAKE_OK = `${JSON.stringify({
  type: "handshake.ok",
  protocol: "tnt.module.v1",
})}\n`;
export const EVENT_OK = '{"type":"event.ok"}\n';

const NOTHING = { reply: null, event: null };
const isString = (value: unknown): value is string => typeof value === "string";

export function encodePost(sender: string, text: string): string {
  return `${JSON.stringify({ type: "message.post", sender, plain_text: text })}\n`;
}

export function parseTntLine(line: string): { reply: string | null; event: TntEvent | null } {
  let record: Record<string, unknown>;
  try {
    const value: unknown = JSON.parse(line);
    if (typeof value !== "object" || value === null || Array.isArray(value)) return NOTHING;
    record = value as Record<string, unknown>;
  } catch {
    return NOTHING;
  }

  switch (record.type) {
    case "handshake":
      return { reply: HANDSHAKE_OK, event: null };
    case "message.created": {
      const { timestamp, sender, plain_text } = (record.message ?? {}) as Record<string, unknown>;
      const valid = isString(timestamp) && isString(sender) && isString(plain_text);
      return {
        reply: EVENT_OK,
        event: valid ? { type: "message.created", message: { timestamp, sender, plain_text } } : null,
      };
    }
    case "presence.snapshot":
      return Array.isArray(record.nicknames)
        ? { reply: null, event: { type: "presence.snapshot", nicknames: record.nicknames.filter(isString) } }
        : NOTHING;
    case "presence.joined":
    case "presence.left":
      return isString(record.nickname)
        ? { reply: null, event: { type: record.type, nickname: record.nickname } }
        : NOTHING;
    default:
      return NOTHING;
  }
}
