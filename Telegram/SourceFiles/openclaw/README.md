# OpenClaw Gateway Abstraction Layer

This directory contains the gateway interface that decouples the UI from
the messaging backend.

## Architecture

```
┌──────────────┐     ┌─────────────────────┐     ┌──────────────────┐
│   Qt UI      │ ──▶ │  GatewayInterface   │ ──▶ │  MtprotoGateway  │  (Phase 1: existing MTProto)
│  (unchanged) │     │  (abstract class)    │     │  OpenClawGateway │  (Phase 2: OC WebSocket)
└──────────────┘     └─────────────────────┘     └──────────────────┘
```

### GatewayInterface.h
Pure virtual abstract class defining what the UI needs:
- Auth state
- Dialog/chat list loading
- Message send/receive/history
- Peer metadata & contacts
- File upload/download
- Real-time update stream
- Search

### Migration Strategy
1. **Phase 1 (current):** `MtprotoGateway` wraps existing `ApiWrap` + `MTP::Sender`
2. **Phase 2:** `OpenClawGateway` implements same interface via OC gateway WebSocket/REST
3. **Phase 3:** Remove `MtprotoGateway` and all MTProto code

The UI layer (`history/`, `dialogs/`, `boxes/`, etc.) should never call MTProto directly —
all protocol access flows through `GatewayInterface`.
