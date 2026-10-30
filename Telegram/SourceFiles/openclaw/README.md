# OpenClaw Gateway Abstraction Layer

This directory decouples the Qt UI from the messaging backend, so the same
client can run against Telegram's MTProto **or** against Hermes — with the UI
none the wiser.

## The end goal

A Telegram Desktop fork that **never talks to Telegram's servers**. The only
backend is **Hermes**. Concretely:

- A **"chat" / dialog** in the UI maps to a **Hermes session**.
- Sending a message = sending a turn into that Hermes session.
- Incoming messages = the assistant/agent's streamed response (and any
  other participants the session exposes).
- Contacts / peer list = the set of Hermes sessions (and, later, agents/people
  the gateway chooses to surface).

The Qt UI — `history/`, `dialogs/`, `boxes/`, `info/`, rendering, animations —
**stays as-is**. All protocol access flows through `GatewayInterface`.

## Architecture

```
┌──────────────┐     ┌─────────────────────┐     ┌──────────────────┐
│   Qt UI      │ ──▶ │  GatewayInterface   │ ──▶ │  MockGateway     │  Phase 1: in-memory fake, no network
│  (unchanged) │     │  (abstract class)   │     │  HermesGateway   │  Phase 2: WS/REST → Hermes
└──────────────┘     └─────────────────────┘     │  MtprotoGateway  │  (legacy, kept only as ref/bridge)
                                                  └──────────────────┘
```

## Strategy — mock first, then Hermes

This is deliberately **not** a "rip out MTProto first" project. We invert it:

1. **Phase 1 — Mock the layer.** Build `MockGateway`, an in-memory
   implementation of `GatewayInterface` that fabricates dialogs, history,
   peers, and a fake update stream. Wire the UI to talk to the gateway instead
   of `ApiWrap`/`MTP::Sender`. **Outcome: the app launches and is fully
   navigable with zero network — no Telegram login, no DC connection.** This
   proves the seam is complete: if the UI works against the mock, it will work
   against Hermes.

2. **Phase 2 — Hermes backend.** Implement `HermesGateway` against the same
   interface, backed by Hermes (WebSocket/REST). Map dialogs↔sessions,
   messages↔turns, the update stream↔response streaming.

3. **Phase 3 — Delete MTProto.** Once `HermesGateway` is the daily driver,
   excise `mtproto/`, the TL schema, `tde2e/`, `passport/`, `export/`, the
   Telegram phone-auth `intro/` flow, etc. (full file map in
   `docs/HERMES-GATEWAY.md`).

### Why mock-first beats delete-first
- The app **stays runnable at every commit** — no months-long "doesn't compile"
  limbo while 66 protocol files are torn out.
- The mock **is the spec.** Whatever the UI demands of the mock is exactly the
  contract `HermesGateway` must satisfy — no guessing.
- MTProto code can be deleted **lazily and safely** once nothing routes to it.

## GatewayInterface.h

Pure-virtual contract for everything the UI needs: auth state, dialog/chat-list
loading, message send/receive/history, peer metadata & contacts, file
upload/download, a real-time update stream, and search. See the header for the
full surface.
