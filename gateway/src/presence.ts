export class Presence {
  #ssh = new Set<string>();
  #web = new Map<string, number>();

  sshSnapshot(nicknames: string[]): void {
    for (const nickname of nicknames) this.#ssh.add(nickname);
  }

  sshJoined(nickname: string): void {
    this.#ssh.add(nickname);
  }

  sshLeft(nickname: string): void {
    this.#ssh.delete(nickname);
  }

  webJoined(handle: string): void {
    this.#web.set(handle, (this.#web.get(handle) ?? 0) + 1);
  }

  webLeft(handle: string): void {
    const count = (this.#web.get(handle) ?? 0) - 1;
    if (count > 0) this.#web.set(handle, count);
    else this.#web.delete(handle);
  }

  online(): string[] {
    return [...new Set([...this.#ssh, ...this.#web.keys()])].sort();
  }
}
