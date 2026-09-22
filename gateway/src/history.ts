import { byteLength } from "./text";

export const ROOM = "lobby";

export type Entry = { timestamp: string; sender: string; text: string };
export type Message = { id: string; room: string; sender: string; text: string; timestamp: string };

const TIMESTAMP = /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$/;
const CONTROL = /[\u0000-\u001f\u007f-\u009f]/;
const TAIL_BYTES = 256 * 1024;
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
  if (fields.length !== 3) return null;
  const [timestamp, sender, stored] = fields as [string, string, string];
  if (!TIMESTAMP.test(timestamp) || sender === "" || stored === "") return null;
  if (byteLength(sender) > 63 || byteLength(stored) > 1023) return null;
  if (CONTROL.test(sender) || CONTROL.test(stored)) return null;
  const text = decodeContent(stored);
  if (text === null || !inWindow(timestamp, now)) return null;
  return { timestamp, sender, text };
}

export function isPublicSender(sender: string): boolean {
  return sender !== "system" && sender !== "系统" && !sender.startsWith("module:");
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

async function tailLines(path: string): Promise<string[]> {
  const file = Bun.file(path);
  if (!(await file.exists())) return [];
  const start = Math.max(0, file.size - TAIL_BYTES);
  const bytes = new Uint8Array(await file.slice(start).arrayBuffer());
  const lines = splitLines(bytes);
  lines.pop();
  if (start > 0) lines.shift();
  return lines.map(decodeLine).filter((line): line is string => line !== null);
}

async function readEntries(path: string, now: number): Promise<Entry[]> {
  return (await tailLines(path))
    .map((line) => parseRecord(line, now))
    .filter((entry): entry is Entry => entry !== null && isPublicSender(entry.sender));
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

  constructor(limit: number) {
    this.#limit = limit;
  }

  add(entry: Entry): Message {
    const occurrence = this.#messages.filter(
      (m) => m.timestamp === entry.timestamp && m.sender === entry.sender && m.text === entry.text,
    ).length;
    const message: Message = {
      id: messageId(entry, occurrence),
      room: ROOM,
      sender: entry.sender,
      text: entry.text,
      timestamp: entry.timestamp,
    };
    this.#messages.push(message);
    if (this.#messages.length > this.#limit) this.#messages.shift();
    return message;
  }

  recent(): Message[] {
    return [...this.#messages];
  }
}
