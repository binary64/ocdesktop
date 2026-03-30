# MTProto Removal Plan — OCDesktop Phase 1

**Date:** 2026-03-31
**Branch:** `feat/mtproto-removal`
**Goal:** Strip Telegram's MTProto protocol layer and prepare the codebase for OpenClaw gateway integration.

## Architecture Overview

The tdesktop codebase has five distinct layers:

| Layer | Location | Role | Action |
|-------|----------|------|--------|
| **Transport** | `mtproto/connection_*.cpp`, `mtproto/details/` | Raw TCP/HTTP/TLS to Telegram DCs | **DELETE** |
| **Protocol** | `mtproto/mtp_instance.*`, `mtproto/session*`, `mtproto/scheme/` | MTProto encryption, serialization, TL schema | **DELETE** |
| **API Facade** | `apiwrap.*`, `api/*` (101 files) | High-level API calls composing MTP requests | **REPLACE** (stub) |
| **Data Models** | `data/*` (207 files) | In-memory data structures consumed by UI | **KEEP** (adapt later) |
| **UI** | `history/`, `dialogs/`, `boxes/`, `info/`, `ui/`, `window/`, etc. | Qt widgets, rendering, animations | **KEEP** |

## File Categorisation

### Category 1: DELETE — Pure Telegram Protocol (66 files)

These are entirely Telegram-specific with zero UI dependency.

**`mtproto/` directory (entire contents):**
- `mtproto/details/` — 18 files (sockets, key creation, TLS, RSA, serialization)
- `mtproto/connection_*.cpp/.h` — 6 files (TCP, HTTP, resolving transports)
- `mtproto/mtp_instance.cpp/.h` — MTProto orchestrator
- `mtproto/session.cpp/.h` — MTProto session management
- `mtproto/session_private.cpp/.h` — Session internals (largest file: 84KB)
- `mtproto/config_loader.cpp/.h` — DC config loading
- `mtproto/special_config_request.cpp/.h` — Fallback config via DNS/HTTP
- `mtproto/mtproto_auth_key.cpp/.h` — Telegram auth key management
- `mtproto/mtproto_concurrent_sender.cpp/.h` — Thread-safe MTP dispatch
- `mtproto/mtproto_config.cpp/.h` — DC configuration
- `mtproto/mtproto_dc_options.cpp/.h` — DC endpoint management
- `mtproto/mtproto_dh_utils.cpp/.h` — Diffie-Hellman key exchange
- `mtproto/mtproto_proxy_data.cpp/.h` — Telegram proxy (MTProxy/SOCKS)
- `mtproto/mtproto_response.cpp/.h` — MTP response parsing
- `mtproto/mtproto_pch.h` — Precompiled header
- `mtproto/core_types.h` — MTP type definitions
- `mtproto/facade.cpp/.h` — Global MTP state
- `mtproto/dedicated_file_loader.cpp/.h` — MTP-based file downloads
- `mtproto/sender.h` — MTP request sender template
- `mtproto/type_utils.h` — MTP type utilities
- `mtproto/scheme/api.tl` — 2914-line Telegram API schema
- `mtproto/scheme/mtproto.tl` — 128-line MTProto schema

**Codegen for scheme:**
- `codegen/scheme/codegen_scheme.py` — TL schema code generator

**CMake targets:**
- `cmake/td_mtproto.cmake` — `td_mtproto` OBJECT library
- `cmake/td_scheme.cmake` — `td_scheme` OBJECT library
- `cmake/generate_scheme.cmake` — Schema generation function

**Telegram-specific features (can delete in Phase 1 or later):**
- `tde2e/` — 4 files (Telegram end-to-end encryption)
- `passport/` — 28 files (Telegram Passport)
- `export/` — 34 files (Telegram data export)
- `intro/` — 20 files (Telegram phone auth flow)
- `calls/` — 83 files (Telegram voice/video calls via tgcalls)
- `inline_bots/` — 20 files (Telegram inline bot queries)
- `support/` — 10 files (Telegram support chat features)

### Category 2: REPLACE — Protocol ↔ UI Bridge (~115 files)

These files currently construct MTP requests. They need stub replacements that maintain the same interface but do nothing (or return empty data).

**`apiwrap.cpp/.h` (5829 lines total):**
- Inherits from `MTP::Sender`
- Every method constructs MTP requests
- Central hub: ALL UI → protocol calls flow through here
- **Action:** Stub all methods to no-op, remove MTP::Sender inheritance

**`api/*` (101 files):**
- Each file handles a specific Telegram API domain
- All use `MTP::Sender _api` member
- **Action:** Stub all to return empty/default results

**MTP-dependent storage files:**
- `storage/download_manager_mtproto.cpp/.h`
- `storage/file_download_mtproto.cpp/.h`
- `storage/file_upload.cpp/.h`
- **Action:** Stub download/upload operations

**MTP-dependent media files:**
- `media/streaming/media_streaming_loader_mtproto.cpp/.h`
- **Action:** Stub loader

**MTP-dependent translation:**
- `lang/translate_mtproto_provider.cpp/.h`
- **Action:** Stub or remove

### Category 3: KEEP — Pure UI & Data (1400+ files)

These files have no direct MTP dependency and form the OCDesktop UI shell:

- `ui/` — 315 files (widgets, controls, effects, animations)
- `history/` — 239 files (message rendering, list widget, bubbles)
- `data/` — 207 files (data models — will adapt types later, not now)
- `info/` — 179 files (info panels, profile pages)
- `boxes/` — 175 files (dialog boxes) — *some reference MTP::Sender, need minimal stubs*
- `media/` — 122 files (media viewer, streaming, player)
- `settings/` — 106 files (settings panels)
- `platform/` — 87 files (OS-specific integration)
- `window/` — 64 files (window management, top bar)
- `core/` — 59 files (application lifecycle)
- `dialogs/` — 56 files (chat list)
- `chat_helpers/` — 51 files (stickers, emoji, GIFs)
- `payments/` — 52 files (payment forms)
- `statistics/` — 38 files (channel statistics)
- `editor/` — 36 files (photo editor)
- `lang/` — 25 files (language/translation)
- `menu/` — 24 files (context menus)
- `main/` — 16 files (session/account — *heavily MTP-dependent, needs stubs*)
- `layout/` — 12 files
- `iv/` — 12 files (instant view)
- `profile/` — 6 files
- `overview/` — 5 files
- `ffmpeg/` — 5 files
- `countries/` — 4 files

### Category 4: KEEP — Libraries

- `lib_base/` — Base utilities (keep)
- `lib_ui/` — UI framework (keep)
- `lib_tl/` — TL type basics (keep — used by generated scheme code; will remove later)
- `lib_crl/` — Concurrency (keep)
- `lib_rpl/` — Reactive programming (keep)
- `lib_lottie/` — Lottie animations (keep)
- `lib_qr/` — QR code rendering (keep)
- `lib_spellcheck/` — Spell checking (keep)
- `lib_storage/` — Storage framework (keep)
- `lib_translate/` — Translation (keep)
- `lib_webview/` — Web view (keep)
- `lib_webrtc/` — WebRTC (keep for now; remove with calls later)
- `ThirdParty/` — GSL, rlottie, tgcalls (keep; tgcalls remove with calls later)

## Dependency Graph — Removal Order

```
td_scheme (TL schema codegen)
    ↑
td_mtproto (protocol implementation)
    ↑
mtproto/* in main target (session, connection, facade)
    ↑
apiwrap / api/* (API facade)
    ↑
boxes, settings, main, window, etc. (UI with MTP::Sender members)
    ↑
history, dialogs, info, ui (pure UI)
```

**Removal must go bottom-up:**

1. **Phase 1a (this PR):** Delete `mtproto/details/`, scheme files, codegen — the pure protocol internals that nothing in UI directly references
2. **Phase 1b (this PR):** Create stub headers for `MTP::Instance`, `MTP::Sender`, `MTP::Error`, `MTP::ProxyData`, `MTP::Config`, `MTP::AuthKey`, `mtpRequestId` — the types that leak into the UI layer
3. **Phase 1c (follow-up):** Stub out `apiwrap` and `api/*` methods
4. **Phase 1d (follow-up):** Delete Telegram-specific features (passport, calls, export, intro, tde2e)

## Stub Interface Design

The UI layer references these MTP types extensively. We need stub headers that provide the same type names but empty implementations:

### `mtproto/mtp_instance.h` (stub)
```cpp
namespace MTP {
class Instance {
public:
    struct Fields {};
    // Minimal interface so Main::Account compiles
};
} // namespace MTP
```

### `mtproto/sender.h` (stub)
```cpp
namespace MTP {
class Sender {
public:
    // All request methods become no-ops
};
} // namespace MTP
```

### `mtproto/core_types.h` (stub)
```cpp
using mtpRequestId = int;
// Minimal type definitions
```

### `mtproto/mtproto_auth_key.h` (stub)
```cpp
namespace MTP {
class AuthKey {
public:
    using Data = std::array<char, 256>;
};
} // namespace MTP
```

### Key types that must survive as stubs:
- `MTP::Instance` — referenced by `Main::Account`, `Core::Application`
- `MTP::Sender` — base class of `ApiWrap`, member of ~30 boxes/api classes
- `MTP::Error` — used in fail callbacks everywhere
- `MTP::Config` — used in startup flow
- `MTP::ProxyData` — used in connection settings
- `MTP::AuthKey` — used in account storage
- `MTP::DcId` — type alias used in statistics
- `mtpRequestId` — used as request tracking ID everywhere
- `MTP::Environment` — enum used in config

## Estimated Effort

| Phase | Description | Effort | Files Changed |
|-------|-------------|--------|---------------|
| 1a | Delete pure protocol internals | 2h | ~40 deleted |
| 1b | Create MTP type stubs | 4h | ~10 new stubs |
| 1c | Stub apiwrap + api/* methods | 8h | ~102 modified |
| 1d | Delete Telegram features | 2h | ~199 deleted |
| 2 | OpenClaw gateway client | 2-3 weeks | ~20 new |
| 3 | Replace api/* with gateway calls | 4-6 weeks | ~102 rewritten |

## Phase 1a: What Gets Deleted Now

### Files to delete:
```
mtproto/details/                          (18 files)
mtproto/scheme/api.tl
mtproto/scheme/mtproto.tl
mtproto/connection_abstract.cpp/.h
mtproto/connection_http.cpp/.h
mtproto/connection_resolving.cpp/.h
mtproto/connection_tcp.cpp/.h
mtproto/config_loader.cpp/.h
mtproto/special_config_request.cpp/.h
mtproto/session.cpp/.h
mtproto/session_private.cpp/.h
mtproto/mtp_instance.cpp                  (keep .h as stub)
mtproto/mtproto_concurrent_sender.cpp     (keep .h as stub)
mtproto/mtproto_config.cpp                (keep .h as stub)
mtproto/mtproto_dc_options.cpp            (keep .h as stub)
mtproto/mtproto_dh_utils.cpp/.h
mtproto/mtproto_proxy_data.cpp            (keep .h as stub)
mtproto/mtproto_response.cpp              (keep .h as stub)
mtproto/mtproto_auth_key.cpp              (keep .h as stub)
mtproto/dedicated_file_loader.cpp         (keep .h as stub)
codegen/scheme/codegen_scheme.py
```

### CMake changes:
- Remove `td_mtproto` target (cmake/td_mtproto.cmake)
- Remove `td_scheme` target (cmake/td_scheme.cmake)
- Remove `generate_scheme.cmake`
- Remove mtproto sources from main CMakeLists.txt
- Remove `tdesktop::td_mtproto` and `tdesktop::td_scheme` link dependencies

### Headers to convert to stubs:
```
mtproto/mtp_instance.h
mtproto/sender.h
mtproto/core_types.h
mtproto/facade.h
mtproto/mtproto_auth_key.h
mtproto/mtproto_config.h
mtproto/mtproto_dc_options.h
mtproto/mtproto_proxy_data.h
mtproto/mtproto_response.h
mtproto/mtproto_concurrent_sender.h
mtproto/mtproto_pch.h
mtproto/type_utils.h
mtproto/dedicated_file_loader.h
```

## Notes

- The `lib_tl` library provides basic TL type infrastructure (`tl_basic_types.h`, `tl_boxed.h`). It's small (4 files) and can stay until Phase 2 when we have no more TL types.
- `facade.cpp/.h` contains global MTP state (pause/unpause). Needs a stub.
- The `storage/` files that reference MTProto (`download_manager_mtproto`, `file_download_mtproto`) will be stubbed in Phase 1c, not deleted — they're part of the main target, not td_mtproto.
- `tgcalls` submodule can be removed when we delete `calls/` in Phase 1d.
