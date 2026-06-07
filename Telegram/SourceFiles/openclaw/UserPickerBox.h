/*
This file is part of OCDesktop,
a native OpenClaw desktop client forked from Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/binary64/ocdesktop/blob/main/LICENSE
*/
#pragma once

#include "base/object_ptr.h"

#include <QString>

namespace Ui {
class GenericBox;
} // namespace Ui

namespace OpenClaw {

// First-run / switch-user picker for the shared household desktop. Presents
// one button per known member (James / Abi); the chosen Hermes user_id is
// reported back via `chosen`. The bridge then filters sessions to that user,
// so each person sees only their own chats. Trust-based (no PIN) by design —
// it's a shared home machine.
void UserPickerBox(
	not_null<Ui::GenericBox*> box,
	const QString &current,
	Fn<void(QString userId)> chosen);

} // namespace OpenClaw
