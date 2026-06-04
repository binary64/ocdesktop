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

namespace OpenClaw {

[[nodiscard]] bool MockModeEnabled();

// Fabricates a self session on the given account (no real auth / MTP login)
// and seeds it with the MockGateway fixtures translated into MTP TL objects,
// pushed through the same data-layer door that real MTProto responses use.
// Returns true if a mock session was created.
bool SeedMockSession(not_null<Main::Account*> account);

} // namespace OpenClaw
