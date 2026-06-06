/*
This file is part of OCDesktop,
a native OpenClaw desktop client forked from Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/binary64/ocdesktop/blob/main/LICENSE
*/
#pragma once

#include <QString>
#include <QtGlobal>

namespace OpenClaw::Sentry {

// ============================================================
// Tier-2 Sentry reporter — a dependency-free crash/error sink.
//
// No sentry-native, no Crashpad: just a hand-built Sentry envelope
// POSTed over QNetworkAccessManager (HTTPS via the OpenSSL backend
// that the static Qt build already links for MTProto). This catches
// the crashes that carry stack context — Qt fatals, assertion
// failures, caught exceptions, and explicit reports from the WS
// gateway path — but NOT raw out-of-process segfaults (that needs
// the heavier Crashpad build, layered in later).
//
// init() also installs a Qt message handler that mirrors qWarning/
// qCritical into a breadcrumb ring buffer and escalates qFatal into
// a captured event before the default handler aborts.
// ============================================================

void init(const QString &release, const QString &environment);
void captureMessage(const QString &level, const QString &message);
void captureException(const QString &type, const QString &value);
void addBreadcrumb(const QString &category, const QString &message);
[[nodiscard]] bool isEnabled();

} // namespace OpenClaw::Sentry
