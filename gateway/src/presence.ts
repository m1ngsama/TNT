export class Presence {
  #ssh = new Set<string>();
  #web = new Map<string, Set<string>>();

  sshSnapshot(nicknames: string[]): void {
    for (const nickname of nicknames) this.#ssh.add(nickname);
  }

  sshJoined(nickname: string): void {
    this.#ssh.add(nickname);
  }

  sshLeft(nickname: string): void {
    this.#ssh.delete(nickname);
  }

  webJoined(nickname: string, connectionId: string): void {
    let connections = this.#web.get(nickname);
    if (!connections) {
      connections = new Set();
      this.#web.set(nickname, connections);
    }
    connections.add(connectionId);
  }

  webLeft(nickname: string, connectionId: string): void {
    const connections = this.#web.get(nickname);
    if (!connections) return;
    connections.delete(connectionId);
    if (connections.size === 0) this.#web.delete(nickname);
  }

  online(): string[] {
    return [...new Set([...this.#ssh, ...this.#web.keys()])].sort();
  }
}
