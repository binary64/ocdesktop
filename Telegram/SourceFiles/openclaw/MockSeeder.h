/*
This file is part of OCDesktop,
a native OpenClaw desktop client forked from Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/binary64/ocdesktop/blob/main/LICENSE
*/
#pragma once

#include "base/basic_types.h"

namespace Main {
class Account;
} // namespace Main

namespace Window {
class Controller;
} // namespace Window

namespace OpenClaw {

class GatewayInterface;

[[nodiscard]] bool MockModeEnabled();
[[nodiscard]] bool SeedingEnabled();

// Returns the persistent live gateway created during seeding, or nullptr
// when no offline/Hermes session is active. The send path uses this to
// route outgoing text to the WS bridge.
[[nodiscard]] GatewayInterface *ActiveGateway();

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

// Test-harness hook (gated on $OCDESKTOP_AUTOSTART_BOT). After seeding,
// deterministically invokes the bot "Start" command path on the seeded bot
// peer — the same ApiWrap::sendBotStart the UI button triggers — so the crash
// class can be reproduced headlessly with no GUI automation. Inert unless the
// env var is set, so it never fires in a normal user session.
void MaybeAutoStartBot(not_null<Main::Account*> account);

} // namespace OpenClaw
