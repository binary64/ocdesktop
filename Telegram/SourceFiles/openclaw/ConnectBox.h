/*
This file is part of OCDesktop,
a native OpenClaw desktop client forked from Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/binary64/ocdesktop/blob/main/LICENSE
*/
#pragma once

#include "base/object_ptr.h"
#include "openclaw/ConnectConfig.h"

namespace Ui {
class GenericBox;
} // namespace Ui

namespace OpenClaw {

// Result of a Connect attempt, reported back to the box so it can either
// close (success) or show an inline error and stay open for a retry.
enum class ConnectResult {
	Success,
	Failed,
};

// Connect screen shown at startup when there's no saved/usable config, or
// after a failed bootstrap. Presents URL + token fields prefilled from
// `initial`. On "Connect", calls `attempt(config, report)` — the caller runs
// the blocking bootstrap and invokes `report` with the outcome. On Success
// the box closes; on Failed it shows an inline error and stays open.
void ConnectBox(
	not_null<Ui::GenericBox*> box,
	ConnectConfig initial,
	Fn<void(ConnectConfig, Fn<void(ConnectResult)>)> attempt);

} // namespace OpenClaw
