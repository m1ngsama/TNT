export type TntEvent =
  | { type: "handshake"; version: string }
  | { type: "message.created"; message: { timestamp: string; sender: string; plain_text: string; verified: boolean } }
  | { type: "identity.requested"; session: string }
  | { type: "private.created"; from: string; to: string; plain_text: string; timestamp: string; verified: boolean }
  | { type: "presence.snapshot"; nicknames: string[] }
  | { type: "presence.joined" | "presence.left"; nickname: string };

export type RoomEvent = Exclude<TntEvent, { type: "handshake" | "identity.requested" | "private.created" }>;
export type PrivateEvent = Extract<TntEvent, { type: "private.created" }>;

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

export function encodeRecord(record: Record<string, string | string[]>): string {
  return `${JSON.stringify(record)}\n`;
}

/** Identity records arrived in TNT 1.5.0; older servers treat them as protocol failures. */
export function supportsIdentity(version: string): boolean {
  const [major = 0, minor = 0] = version.split(".").map(Number);
  return major > 1 || (major === 1 && minor >= 5);
}

/** Private messages and app presence arrived in TNT 1.6.0. */
export function supportsPrivate(version: string): boolean {
  const [major = 0, minor = 0] = version.split(".").map(Number);
  return major > 1 || (major === 1 && minor >= 6);
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
    case "handshake": {
      const version = (record.server as Record<string, unknown> | undefined)?.version;
      return { reply: HANDSHAKE_OK, event: { type: "handshake", version: isString(version) ? version : "" } };
    }
    case "message.created": {
      const { timestamp, sender, plain_text, metadata } = (record.message ?? {}) as Record<string, unknown>;
      const valid = isString(timestamp) && isString(sender) && isString(plain_text);
      const verified = (metadata as Record<string, unknown> | undefined)?.verified === true;
      return {
        reply: EVENT_OK,
        event: valid ? { type: "message.created", message: { timestamp, sender, plain_text, verified } } : null,
      };
    }
    case "private.created": {
      const { from, to, plain_text, timestamp } = record;
      return isString(from) && isString(to) && isString(plain_text) && isString(timestamp)
        ? { reply: null, event: { type: "private.created", from, to, plain_text, timestamp, verified: record.verified === true } }
        : NOTHING;
    }
    case "identity.requested":
      return isString(record.session) ? { reply: null, event: { type: "identity.requested", session: record.session } } : NOTHING;
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
