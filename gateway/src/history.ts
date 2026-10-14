import { byteLength } from "./text";

export const ROOM = "lobby";

export type Entry = { timestamp: string; sender: string; text: string; verified?: boolean };
export type Kind = "text" | "action" | "system" | "module";
export type Message = {
  id: string;
  room: string;
  sender: string;
  text: string;
  timestamp: string;
  kind: Kind;
  mentions: string[];
  verified: boolean;
};

const TIMESTAMP = /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$/;
const CONTROL = /[\u0000-\u001f\u007f-\u009f]/;
export const RECENT = 100;
const READ_BYTES = 8 * 1024 * 1024;
const DAY_MS = 86_400_000;
const WINDOW_PAST_MS = 3650 * DAY_MS;
const WINDOW_FUTURE_MS = DAY_MS;

const utf8Decoder = new TextDecoder("utf-8", { fatal: true });

function inWindow(timestamp: string, now: number): boolean {
  const t = Date.parse(timestamp);
  return t <= now + WINDOW_FUTURE_MS && t >= now - WINDOW_PAST_MS;
}

export function decodeContent(stored: string): string | null {
  let text = "";
  for (let i = 0; i < stored.length; i++) {
    if (stored[i] !== "\\") {
      text += stored[i];
      continue;
    }
    const next = stored[++i];
    if (next === "\\") text += "\\";
    else if (next === "n") text += "\n";
    else return null;
  }
  return text;
}

export function parseRecord(line: string, now: number): Entry | null {
  const fields = line.split("|");
  // Version 3 adds a flags field; "v" marks a sender who was signed in.
  if (fields.length !== 3 && fields.length !== 4) return null;
  const [timestamp, sender, stored, flags = ""] = fields as [string, string, string, string?];
  if (!TIMESTAMP.test(timestamp) || sender === "" || stored === "") return null;
  if (byteLength(sender) > 63 || byteLength(stored) > 1023) return null;
  if (CONTROL.test(sender) || CONTROL.test(stored)) return null;
  const text = decodeContent(stored);
  if (text === null || !inWindow(timestamp, now)) return null;
  return flags.includes("v") ? { timestamp, sender, text, verified: true } : { timestamp, sender, text };
}

const MENTION = /@([A-Za-z0-9_]+)/g;

export function mentions(text: string): string[] {
  return [...new Set([...text.matchAll(MENTION)].map((m) => m[1]!.toLowerCase()))];
}

export function classify(entry: Entry): { kind: Kind; sender: string; text: string } {
  if (entry.sender === "*") {
    const space = entry.text.indexOf(" ");
    return space > 0
      ? { kind: "action", sender: entry.text.slice(0, space), text: entry.text.slice(space + 1) }
      : { kind: "action", sender: entry.text, text: "" };
  }
  if (entry.sender === "system" || entry.sender === "系统") return { kind: "system", sender: entry.sender, text: entry.text };
  if (entry.sender.startsWith("module:")) return { kind: "module", sender: entry.sender.slice(7), text: entry.text };
  return { kind: "text", sender: entry.sender, text: entry.text };
}

function splitLines(bytes: Uint8Array): Uint8Array[] {
  const lines: Uint8Array[] = [];
  let start = 0;
  for (let i = 0; i < bytes.length; i++) {
    if (bytes[i] === 0x0a) {
      lines.push(bytes.subarray(start, i));
      start = i + 1;
    }
  }
  lines.push(bytes.subarray(start));
  return lines;
}

function decodeLine(bytes: Uint8Array): string | null {
  try {
    return utf8Decoder.decode(bytes);
  } catch {
    return null;
  }
}

async function readLines(path: string): Promise<string[]> {
  const file = Bun.file(path);
  if (!(await file.exists())) return [];
  const start = Math.max(0, file.size - READ_BYTES);
  const lines = splitLines(new Uint8Array(await file.slice(start).arrayBuffer()));
  lines.pop();
  if (start > 0) lines.shift();
  return lines.map(decodeLine).filter((line): line is string => line !== null);
}

async function readEntries(path: string, now: number): Promise<Entry[]> {
  return (await readLines(path))
    .map((line) => parseRecord(line, now))
    .filter((entry): entry is Entry => entry !== null);
}

export async function readHistory(stateDir: string, limit: number, now: number = Date.now()): Promise<Entry[]> {
  let entries = await readEntries(`${stateDir}/messages.log`, now);
  if (entries.length < limit) {
    entries = [...(await readEntries(`${stateDir}/messages.log.1`, now)), ...entries];
  }
  return entries.slice(-limit);
}

export function messageId(entry: Entry, occurrence: number): string {
  return new Bun.CryptoHasher("sha256")
    .update(`${entry.timestamp}|${entry.sender}|${entry.text}|${occurrence}`)
    .digest("hex")
    .slice(0, 16);
}

export class Ledger {
  readonly #limit: number;
  #messages: Message[] = [];
  #entries: Entry[] = [];

  constructor(limit: number) {
    this.#limit = limit;
  }

  add(entry: Entry): Message {
    const occurrence = this.#entries.filter(
      (m) => m.timestamp === entry.timestamp && m.sender === entry.sender && m.text === entry.text,
    ).length;
    const shown = classify(entry);
    const message: Message = {
      id: messageId(entry, occurrence),
      room: ROOM,
      sender: shown.sender,
      text: shown.text,
      timestamp: entry.timestamp,
      kind: shown.kind,
      mentions: mentions(shown.text),
      verified: entry.verified === true && (shown.kind === "text" || shown.kind === "action"),
    };
    this.#messages.push(message);
    this.#entries.push(entry);
    if (this.#messages.length > this.#limit) {
      this.#messages.shift();
      this.#entries.shift();
    }
    return message;
  }

  recent(): Message[] {
    return this.#messages.slice(-RECENT);
  }

  /** Newest first, matching sender or text without regard to case, like :search. */
  search(query: string, limit: number): Message[] {
    const wanted = query.toLowerCase();
    const found: Message[] = [];
    for (let i = this.#messages.length - 1; i >= 0 && found.length < limit; i--) {
      const message = this.#messages[i]!;
      if (message.kind === "system") continue;
      if (message.sender.toLowerCase().includes(wanted) || message.text.toLowerCase().includes(wanted)) found.push(message);
    }
    return found;
  }

  before(id: string, limit: number): { messages: Message[]; more: boolean } | null {
    const index = this.#messages.findIndex((m) => m.id === id);
    if (index < 0) return null;
    const start = Math.max(0, index - limit);
    return { messages: this.#messages.slice(start, index), more: start > 0 };
  }
}
