/*
This file is part of OCDesktop,
a native OpenClaw desktop client forked from Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/binary64/ocdesktop/blob/main/LICENSE
*/
#pragma once

#include "base/object_ptr.h"
#include "openclaw/ConnectConfig.h"

#include <QString>
#include <vector>

namespace Ui {
class GenericBox;
} // namespace Ui

namespace OpenClaw {

// First-run / switch-user picker for the shared household desktop. Presents
// one button per known member (roster fetched live from the bridge; falls
// back to a generic offline list if unreachable); the chosen Hermes user_id
// is reported back via `chosen`. The bridge then filters sessions to that
// user, so each person sees only their own chats. Trust-based (no PIN) by
// design — it's a shared home machine.
void UserPickerBox(
	not_null<Ui::GenericBox*> box,
	std::vector<KnownUser> members,
	const QString &current,
	Fn<void(QString userId)> chosen);

} // namespace OpenClaw
