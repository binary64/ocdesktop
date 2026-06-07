/*
This file is part of OCDesktop,
a native OpenClaw desktop client forked from Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/binary64/ocdesktop/blob/main/LICENSE
*/
#pragma once

#include <QString>
#include <vector>

namespace OpenClaw {

// Persistent connection settings for the Hermes WS bridge, stored as a
// small JSON file in the app working dir (cWorkingDir()/ocdesktop-connect.json).
// Lets the user type the URL/token once via the ConnectBox and have it
// remembered across launches, instead of relying on env vars / AppRun.
struct ConnectConfig {
	QString url;
	QString token;
	QString user;

	[[nodiscard]] bool valid() const {
		return !url.isEmpty();
	}

	[[nodiscard]] bool hasUser() const {
		return !user.isEmpty();
	}
};

// The two household members served by the shared bridge. The id is the
// Hermes user_id (Telegram sender id) the bridge filters sessions on; the
// label is what the picker shows. Kept here so the picker, the gateway auth
// frame, and the menu "switch user" entry all agree on one source of truth.
struct KnownUser {
	QString id;
	QString label;
};

[[nodiscard]] inline std::vector<KnownUser> KnownUsers() {
	return {
		{ u"7267310819"_q, u"James"_q },
		{ u"8462750275"_q, u"Abi"_q },
		{ u"system"_q, u"System"_q },
	};
}

[[nodiscard]] inline QString UserDisplayName(const QString &id) {
	for (const auto &u : KnownUsers()) {
		if (u.id == id) {
			return u.label;
		}
	}
	return id;
}

// Reads saved config; returns an empty (invalid) config if none on disk.
[[nodiscard]] ConnectConfig LoadConnectConfig();

// Writes config to disk. Returns false on write failure.
bool SaveConnectConfig(const ConnectConfig &config);

// Resolves the effective config at startup, in priority order:
//   1. Saved JSON config (user typed it before)
//   2. Environment (OCDESKTOP_HERMES_URL + token/env-file) — back-compat
// Returns an invalid config when nothing is set, signalling the caller to
// present the ConnectBox.
[[nodiscard]] ConnectConfig ResolveConnectConfig();

} // namespace OpenClaw
