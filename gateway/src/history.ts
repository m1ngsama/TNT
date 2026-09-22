import { byteLength } from "./text";

export const ROOM = "lobby";

export type Entry = { timestamp: string; sender: string; text: string };
export type Message = { id: string; room: string; sender: string; text: string; timestamp: string };

const TIMESTAMP = /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$/;
const CONTROL = /[\u0000-\u001f\u007f-\u009f]/;
const TAIL_BYTES = 256 * 1024;

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

export function parseRecord(line: string): Entry | null {
  const fields = line.split("|");
  if (fields.length !== 3) return null;
  const [timestamp, sender, stored] = fields as [string, string, string];
  if (!TIMESTAMP.test(timestamp) || sender === "" || stored === "") return null;
  if (byteLength(sender) > 63 || byteLength(stored) > 1023) return null;
  if (CONTROL.test(sender) || CONTROL.test(stored)) return null;
  const text = decodeContent(stored);
  return text === null ? null : { timestamp, sender, text };
}

export function isPublicSender(sender: string): boolean {
  return sender !== "system" && sender !== "系统" && !sender.startsWith("module:");
}

async function tailLines(path: string): Promise<string[]> {
  const file = Bun.file(path);
  if (!(await file.exists())) return [];
  const start = Math.max(0, file.size - TAIL_BYTES);
  const lines = (await file.slice(start).text()).split("\n");
  lines.pop();
  if (start > 0) lines.shift();
  return lines;
}

async function readEntries(path: string): Promise<Entry[]> {
  return (await tailLines(path))
    .map(parseRecord)
    .filter((entry): entry is Entry => entry !== null && isPublicSender(entry.sender));
}

export async function readHistory(stateDir: string, limit: number): Promise<Entry[]> {
  let entries = await readEntries(`${stateDir}/messages.log`);
  if (entries.length < limit) {
    entries = [...(await readEntries(`${stateDir}/messages.log.1`)), ...entries];
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
