# OCDesktop ↔ Hermes WebSocket Protocol (v1)

Status: **draft / implementing**. Defines the wire contract between the
OCDesktop C++ client (`WsGateway : GatewayInterface`) and the Hermes-side
WS bridge (a Hermes platform-adapter plugin).

## Design

The bridge is **Decision A**: a Hermes plugin platform adapter. It reuses the
proven `api_server` path — `SessionDB.list_sessions_rich()` / `get_messages()`
for reads, and `AIAgent.run_conversation(stream_delta_callback=...)` for
send+stream. Exposed over WebSocket, Tailscale-private, bearer-token auth.

### Identity mapping

| Hermes concept            | Gateway concept (`GatewayInterface`) |
|---------------------------|--------------------------------------|
| session (one per chat)    | `GatewayDialog` + `GatewayPeer`      |
| session `id` (string)     | `PeerId` (uint64) — stable hash      |
| message turn (user)       | `GatewayMessage` `fromId == self(1)` |
| message turn (assistant)  | `GatewayMessage` `fromId == peerId`  |
| live token stream         | `update` frames → `UpdateHandler`    |

`PeerId = (xxhash64(session_id) | 1)` — lower bit forced so it never collides
with `self` (id `1`) and never lands on `0`. The bridge keeps the reverse map
`peerId → session_id` in memory, rebuilt on every `sessions.list`.

`self` is always peer id `1` ("You"). Each Hermes session surfaces as one peer
whose id is the hashed session id; assistant turns are attributed to that peer,
user turns to `self`.

## Transport

- WebSocket, JSON text frames, one JSON object per frame.
- Client opens `wss://<tailscale-host>/ocdesktop` (or `ws://` on LAN).
- First frame MUST be `auth`. Server rejects all other ops until authed.
- Request frames carry a client-assigned integer `id`; the matching response
  echoes it. Server-initiated frames (`update`) carry no `id`.

## Client → Server

```jsonc
{ "id": 1, "op": "auth", "token": "<bearer>" }
{ "id": 2, "op": "sessions.list" }
{ "id": 3, "op": "history", "peerId": 12345, "limit": 50, "offsetId": 0 }
{ "id": 4, "op": "message.send", "peerId": 12345, "text": "hello", "replyTo": 0 }
{ "id": 5, "op": "typing", "peerId": 12345, "typing": true }
{ "id": 6, "op": "peer.full", "peerId": 12345 }
```

## Server → Client (responses, `id` echoed)

```jsonc
// auth
{ "id": 1, "ok": true, "result": { "self": { "id": 1, "name": "You" } } }

// sessions.list  → dialogs + peers (mirrors mock.json shape)
{ "id": 2, "ok": true, "result": {
    "peers":   [ { "id": 12345, "name": "Powerlinks", "username": "...", "about": "...", "isBot": true } ],
    "dialogs": [ { "peerId": 12345, "pinned": false, "archived": false, "unreadCount": 0, "topMessageId": 999 } ]
} }

// history → messages, ascending by id
{ "id": 3, "ok": true, "result": {
    "messages": [ { "id": 1001, "peerId": 12345, "fromId": 1, "text": "...", "date": 1717480800, "replyToMsgId": 0 } ]
} }

// message.send → immediate ack echoing the stored user message
{ "id": 4, "ok": true, "result": {
    "message": { "id": 2001, "peerId": 12345, "fromId": 1, "text": "hello", "date": 1717481000 }
} }

// error
{ "id": 4, "ok": false, "error": "session not found" }
```

## Server → Client (updates, no `id`)

These drive `GatewayInterface::UpdateHandler`. The assistant reply streams as
deltas, then a final `message.new` carries the committed message.

```jsonc
{ "op": "update", "kind": "typing",       "peerId": 12345, "typing": true }
{ "op": "update", "kind": "stream.start", "peerId": 12345, "msgId": 2002 }
{ "op": "update", "kind": "stream.delta", "peerId": 12345, "msgId": 2002, "text": "Mor" }
{ "op": "update", "kind": "stream.delta", "peerId": 12345, "msgId": 2002, "text": "ning" }
{ "op": "update", "kind": "message.new",  "peerId": 12345,
    "message": { "id": 2002, "peerId": 12345, "fromId": 12345, "text": "Morning, James.", "date": 1717481005 } }
{ "op": "update", "kind": "typing",       "peerId": 12345, "typing": false }
```

### Streaming → UI mapping

`stream.start` creates a placeholder assistant message in the History with
`msgId`. Each `stream.delta` appends text to it (the client edits the existing
bubble). `message.new` with the same `msgId` finalises it. This reuses
tdesktop's existing "edit message" path, so streaming renders as a single
growing bubble — exactly how the agent feels in the CLI.

## Auth & exposure

- Bearer token set on the Hermes side (`OCDESKTOP_WS_TOKEN`), entered in the
  client's connect screen alongside the URL.
- Bind to the Tailscale interface only; never `0.0.0.0` publicly. authentik can
  front it for browser-origin cases, but the native client uses the bearer.
- `auth` failure → close with code `4001`.

## Versioning

`auth` response MAY include `"protocol": 1`. Client refuses mismatched majors.
New ops are additive; unknown ops return `{ "ok": false, "error": "unknown op" }`.
