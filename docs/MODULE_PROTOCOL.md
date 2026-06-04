# TNT Module Protocol

This document defines the compatibility contract for external TNT modules.
The first implementation target is external-process modules that exchange
JSON Lines with TNT over stdin/stdout. Keeping modules out of the server
address space makes the community extension surface easier to audit, restart,
rate-limit, and disable.

The protocol is intentionally separate from `messages.log` v1. TNT 1.x keeps
the persisted public history format stable. Module-generated content must
always provide a plain-text fallback that can be stored and rendered by older
or less capable clients.

TNT core should stay conservative: text-first, terminal-compatible, and easy
to deploy over plain SSH. Modules are the extension surface for personalized
workflow features, rich rendering, terminal-specific visuals, and other
experience experiments. Integrating a module with TNT must not make plain
terminal users lose the basic chat path.

## Compatibility

- Protocol version: `tnt.module.v1`
- Transport: UTF-8 JSON Lines
- Framing: one complete JSON object per line
- Direction: TNT sends events to module stdin; modules write responses to
  stdout
- Error stream: modules should write diagnostics to stderr
- License: module protocol examples and official community modules should use
  the same license as TNT unless a module states stricter terms

Modules are disabled unless `TNT_MODULE_PATHS` is set. The value is a
colon-separated list of module directories, each containing `tnt-module.json`
and the declared executable entrypoint.

TNT may add optional fields to existing messages. Modules must ignore unknown
fields. TNT must ignore unknown response fields unless the response type
explicitly requires them.

## Manifest

Each module directory should include `tnt-module.json`:

```json
{
  "protocol": "tnt.module.v1",
  "name": "echo",
  "version": "0.1.0",
  "description": "Echoes public messages for testing",
  "entrypoint": "./echo-module.sh",
  "permissions": ["message:read", "message:create"],
  "events": ["message.created"]
}
```

Required fields:

- `protocol`: protocol compatibility string
- `name`: stable module id, lowercase ASCII, `a-z`, `0-9`, and `-`
- `version`: module version
- `entrypoint`: executable path relative to the manifest directory
- `permissions`: explicit capabilities requested by the module
- `events`: event names the module wants to receive

## Handshake

TNT starts a module process and writes a handshake event:

```json
{"type":"handshake","protocol":"tnt.module.v1","server":{"name":"tnt","version":"1.0.1"}}
```

The module should answer:

```json
{"type":"handshake.ok","protocol":"tnt.module.v1","module":{"name":"echo","version":"0.1.0"}}
```

If the module cannot run, it should answer:

```json
{"type":"error","code":"unsupported_protocol","message":"requires tnt.module.v2"}
```

## Events

Message-created event:

```json
{
  "type": "message.created",
  "message": {
    "id": "local-00000001",
    "timestamp": "2026-06-04T12:00:00Z",
    "sender": "alice",
    "kind": "text",
    "plain_text": "hello",
    "metadata": {}
  }
}
```

The `plain_text` field is mandatory for every user-visible message. Future
rich content, images, and terminal-specific render hints must be represented
as optional metadata or attachment records with a plain-text fallback.

## Responses

Create a public message:

```json
{"type":"message.create","plain_text":"echo: hello"}
```

No-op acknowledgement:

```json
{"type":"event.ok"}
```

Module error:

```json
{"type":"error","code":"bad_request","message":"missing plain_text"}
```

## Security Rules

- Modules are untrusted external processes.
- TNT should enforce per-module permissions before delivering events or
  accepting responses.
- TNT should cap stdout line length, startup time, event handling time, and
  total queued output.
- TNT should disable a module after repeated invalid JSON, protocol errors, or
  timeout failures.
- Modules must never receive private messages unless they request and are
  granted an explicit private-message permission.

## Rendering Rules

Every module-created message must be renderable as plain text. Terminal image
protocols such as Kitty graphics or Sixel are optional renderer capabilities,
not message requirements. A module may provide attachment metadata later, but
TNT must be able to fall back to a link, filename, digest, or short label.
