const encoder = new TextEncoder();
const CONTROL = /[\u0000-\u001f\u007f-\u009f]/;
const TEXT_CONTROL = /[\u0000-\u0009\u000b-\u001f\u007f-\u009f]/;
const NICKNAME_ILLEGAL = /[|;&$`<>(){}[\]'"\\:]/;
const RESERVED = new Set(["*", "system", "系统"]);

export const MAX_TEXT_BYTES = 1023;

export function byteLength(text: string): number {
  return encoder.encode(text).length;
}

export function isValidNickname(name: string): boolean {
  return (
    name !== "" &&
    !" .-".includes(name[0]!) &&
    !RESERVED.has(name) &&
    !name.startsWith("module:") &&
    !CONTROL.test(name) &&
    !NICKNAME_ILLEGAL.test(name) &&
    [...name].length <= 20 &&
    byteLength(name) < 64
  );
}

export function normalizeText(text: string): string {
  return text.replace(/[|\r]/g, " ");
}

export function validateText(input: unknown): string | null {
  if (typeof input !== "string" || !input.isWellFormed()) return null;
  const text = normalizeText(input);
  if (text.trim() === "" || TEXT_CONTROL.test(text)) return null;
  const escapes = text.match(/[\n\\]/g)?.length ?? 0;
  return byteLength(text) + escapes <= MAX_TEXT_BYTES ? text : null;
}
