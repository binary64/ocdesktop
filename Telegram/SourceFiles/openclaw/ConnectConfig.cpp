/*
This file is part of OCDesktop,
a native OpenClaw desktop client forked from Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/binary64/ocdesktop/blob/main/LICENSE
*/
#include "openclaw/ConnectConfig.h"

#include "settings.h"
#include "logs.h"

#include <QProcessEnvironment>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>

namespace OpenClaw {
namespace {

[[nodiscard]] QString ConfigPath() {
	return cWorkingDir() + u"ocdesktop-connect.json"_q;
}

[[nodiscard]] ConnectConfig FromEnvironment() {
	const auto env = QProcessEnvironment::systemEnvironment();
	auto config = ConnectConfig();
	config.url = env.value("OCDESKTOP_HERMES_URL");
	if (config.url.isEmpty()) {
		return config;
	}
	config.user = env.value("OCDESKTOP_HERMES_USER");
	config.token = env.value("OCDESKTOP_HERMES_TOKEN");
	if (config.token.isEmpty()) {
		const auto envFile = env.value("OCDESKTOP_HERMES_ENV");
		if (!envFile.isEmpty()) {
			QFile f(envFile);
			if (f.open(QIODevice::ReadOnly)) {
				const auto contents = QString::fromUtf8(f.readAll());
				for (const auto &line : contents.split('\n')) {
					if (line.startsWith("OCDESKTOP_WS_TOKEN=")) {
						config.token = line
							.mid(QString("OCDESKTOP_WS_TOKEN=").size())
							.trimmed();
					}
				}
			}
		}
	}
	return config;
}

} // namespace

ConnectConfig LoadConnectConfig() {
	auto config = ConnectConfig();
	QFile f(ConfigPath());
	if (!f.open(QIODevice::ReadOnly)) {
		return config;
	}
	const auto doc = QJsonDocument::fromJson(f.readAll());
	if (!doc.isObject()) {
		return config;
	}
	const auto obj = doc.object();
	config.url = obj.value("url").toString();
	config.token = obj.value("token").toString();
	config.user = obj.value("user").toString();
	return config;
}

bool SaveConnectConfig(const ConnectConfig &config) {
	auto obj = QJsonObject();
	obj.insert("url", config.url);
	obj.insert("token", config.token);
	obj.insert("user", config.user);
	const auto bytes = QJsonDocument(obj).toJson(QJsonDocument::Indented);

	QFile f(ConfigPath());
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		LOG(("OpenClaw connect: failed to write config to %1").arg(ConfigPath()));
		return false;
	}
	f.write(bytes);
	f.close();
	LOG(("OpenClaw connect: saved config to %1").arg(ConfigPath()));
	return true;
}

ConnectConfig ResolveConnectConfig() {
	auto saved = LoadConnectConfig();
	if (saved.valid()) {
		return saved;
	}
	return FromEnvironment();
}

} // namespace OpenClaw
