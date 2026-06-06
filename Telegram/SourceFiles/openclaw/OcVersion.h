/*
This file is part of OCDesktop,
a Hermes-bridge fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/binary64/ocdesktop/blob/main/LICENSE
*/
#pragma once

#include <QtCore/QString>

namespace OpenClaw {

// OCDesktop fork build number. Bump this on every shipped build so the
// in-app footer reflects whether the user is on the latest fork build,
// independently of the upstream Telegram Desktop version it tracks.
inline constexpr int kOcBuild = 6;

[[nodiscard]] inline QString OcVersionText() {
	return u"OC build %1"_q.arg(kOcBuild);
}

[[nodiscard]] inline QString OcAppName() {
	return u"OCDesktop"_q;
}

[[nodiscard]] inline QString OcHomeUrl() {
	return u"https://github.com/binary64/ocdesktop"_q;
}

} // namespace OpenClaw
