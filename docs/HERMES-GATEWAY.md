# OCDesktop → Hermes Gateway

**Branch:** `feat/hermes-gateway`
**Base:** clean `main` (tracks upstream tdesktop `dev`, currently v6.8.4+)
**Supersedes:** the earlier `feat/mtproto-removal` branch (closed PR #3), whose
`GatewayInterface.h` and file-categorisation are carried forward here.

## Vision

Turn Telegram Desktop into a native client that **only ever talks to Hermes** —
never to Telegram's servers. The mapping:

| Telegram concept | Becomes |
|------------------|---------|
| Chat / dialog | A **Hermes session** |
| Sending a message | Sending a **turn** into that session |
| Incoming message | The agent's **streamed response** (+ other session participants) |
| Contact / peer list | The set of Hermes sessions / agents the gateway surfaces |
| Login / phone auth | Hermes auth (no SMS code, no DC handshake) |

The Qt UI is a mature, polished messaging client. Rather than build a chat UI
from scratch for Hermes, we **keep the UI and swap the backend** behind a single
abstraction: `GatewayInterface`.

## Approach: mock first, then Hermes (NOT delete first)

The previous plan led with "delete 66 MTProto files". That leaves the app
un-compilable for a long stretch and forces us to guess the backend contract.
We invert it:

### Phase 1 — Mock the layer  ← *this PR establishes the skeleton*
- `GatewayInterface.h` — the pure-virtual contract (auth, dialogs, messages,
  peers, files, update stream, search).
- `MockGateway` — an in-memory implementation: fabricated dialogs, history,
  peers, and a synthetic update stream. **No network.**
- Route the UI's protocol calls through the gateway instead of
  `ApiWrap` / `MTP::Sender`.
- **Done when:** the client launches and is fully navigable with zero network
  and no Telegram login. The mock *is* the spec for Phase 2.

### Phase 2 — HermesGateway
- Implement `GatewayInterface` against Hermes (WebSocket/REST).
- dialogs ↔ sessions, messages ↔ turns, update stream ↔ response streaming.
- Auth ↔ Hermes auth.

### Phase 3 — Excise MTProto
Once HermesGateway is the daily driver, delete the Telegram-only layers below.

---

## Codebase layers (from upstream analysis)

| Layer | Location | Role | Eventual action |
|-------|----------|------|------|
| **Transport** | `mtproto/connection_*`, `mtproto/details/` | Raw TCP/HTTP/TLS to Telegram DCs | DELETE (Phase 3) |
| **Protocol** | `mtproto/mtp_instance.*`, `mtproto/session*`, `mtproto/scheme/` | MTProto crypto, serialization, TL schema | DELETE (Phase 3) |
| **API Facade** | `apiwrap.*`, `api/*` (~101 files) | High-level calls composing MTP requests | REROUTE → gateway |
| **Data Models** | `data/*` (~207 files) | In-memory structures the UI consumes | KEEP (adapt) |
| **UI** | `history/`, `dialogs/`, `boxes/`, `info/`, `ui/`, `window/` | Qt widgets, rendering | KEEP unchanged |

### Phase-3 DELETE candidates (Telegram-only, ~66 core + features)
- `mtproto/` entire directory — transports, sessions, auth keys, DH, DC config,
  proxy, TL schema (`api.tl` ~2900 lines, `mtproto.tl`).
- `codegen/scheme/`, `cmake/td_mtproto.cmake`, `cmake/td_scheme.cmake`,
  `cmake/generate_scheme.cmake`.
- Telegram-specific features: `tde2e/`, `passport/`, `export/`, `intro/`
  (phone auth), `calls/` (tgcalls), `inline_bots/`, `support/`.

### REROUTE (Phase 1–2): protocol ↔ UI bridge
- `apiwrap.cpp/.h` (~5800 lines) — central hub; every UI→protocol call flows
  here. Inherits `MTP::Sender`. Route through `GatewayInterface` instead.
- `api/*` (~101 files) — each a Telegram API domain via `MTP::Sender _api`.
- MTP-backed storage/media: `storage/download_manager_mtproto.*`,
  `storage/file_download_mtproto.*`, `storage/file_upload.*`,
  `media/streaming/media_streaming_loader_mtproto.*`.

## Build note
Base tracks upstream `dev` (Qt **6.11.1** as of 6.8.3). Linux build deps in
`../Libraries` must match. The Linux/CI build fixes on
`fix/linux-cmake-qt-private` (cmake `Qt::WidgetsPrivate` fix, OOM swap limits,
kaniko Dockerfile) are a separate workstream worth rebasing onto `main` to get a
green build for testing this gateway work.

## Status (this PR)
- [x] `GatewayInterface.h` — contract (carried from old branch)
- [x] `openclaw/README.md` — architecture + mock-first rationale
- [x] `MockGateway.h` — Phase-1 skeleton (declarations)
- [ ] `MockGateway.cpp` — fixture data + synthetic update stream
- [ ] CMake wiring for the `openclaw/` sources
- [ ] First UI call site rerouted through the gateway (proof of seam)
