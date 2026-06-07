/*
This file is part of OCDesktop,
a native OpenClaw desktop client forked from Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/binary64/ocdesktop/blob/main/LICENSE
*/
#pragma once

#include "base/basic_types.h"

#include <optional>

class History;

namespace Main {
class Account;
} // namespace Main

namespace Window {
class Controller;
} // namespace Window

namespace Data {
enum class LoadDirection : char;
} // namespace Data

namespace OpenClaw {

class GatewayInterface;

[[nodiscard]] bool MockModeEnabled();
[[nodiscard]] bool SeedingEnabled();

// Per-peer metadata registry (source tag + Hermes session id), populated from
// the bridge's sessions.list/dialog.new peer payloads. The chat header uses it
// to render "Hermes (source · sessionid)" for the counterparty. Stored in a
// static map keyed by bare peer id so the top-bar widget can look it up without
// a gateway handle.
void RememberPeerMeta(uint64 peerId, const QString &source, const QString &session);
[[nodiscard]] QString PeerHeaderLabel(uint64 peerId);

// Returns the persistent live gateway created during seeding, or nullptr
// when no offline/Hermes session is active. The send path uses this to
// route outgoing text to the WS bridge.
[[nodiscard]] GatewayInterface *ActiveGateway();

// Offline history provider. When a chat is opened in a gateway-backed
// (offline) session, HistoryWidget::firstLoadMessages would fire a dead
// MTProto messages.getHistory that never returns (AUTH_KEY_UNREGISTERED), so
// the conversation renders empty. This synthesises an MTPmessages_Messages
// from the gateway's cached history for the peer, which the caller feeds
// straight into messagesReceived() — the same door a real MTProto response
// uses. Returns std::nullopt when no offline gateway session is active (so
// the caller falls through to the normal MTProto path).
[[nodiscard]] std::optional<MTPmessages_Messages> OfflineHistory(
	not_null<History*> history);

// Fabricates a self session on the given account (no real auth / MTP login)
// and seeds it with the gateway fixtures translated into MTP TL objects,
// pushed through the same data-layer door that real MTProto responses use.
// Returns true if a session was created.
bool SeedMockSession(not_null<Main::Account*> account);

// Drives the whole startup connect flow: resolves saved/env config, seeds if
// possible, otherwise shows the ConnectBox so the user can type the bridge
// URL + token. On a successful connect the config is persisted so future
// launches are instant. Safe to call once from startDomain().
void StartConnectFlow(
	not_null<Main::Account*> account,
	Window::Controller *window);

// Drops the current user selection, tears down the live session/gateway, and
// re-runs the connect flow so the household picker reappears. Wired to the
// "Switch user" entry in the main menu.
void SwitchUser(not_null<Main::Account*> account);

// Test-harness hook (gated on $OCDESKTOP_AUTOSTART_BOT). After seeding,
// deterministically invokes the bot "Start" command path on the seeded bot
// peer — the same ApiWrap::sendBotStart the UI button triggers — so the crash
// class can be reproduced headlessly with no GUI automation. Inert unless the
// env var is set, so it never fires in a normal user session.
void MaybeAutoStartBot(not_null<Main::Account*> account);

} // namespace OpenClaw
