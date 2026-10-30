/*
This file is part of OCDesktop,
a native OpenClaw desktop client forked from Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/binary64/ocdesktop/blob/main/LICENSE
*/
#include "openclaw/SentryReporter.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUuid>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QSysInfo>
#include <QUrl>

namespace OpenClaw::Sentry {
namespace {

QNetworkAccessManager *Manager = nullptr;
QString IngestUrl;
QString Release;
QString Environment;
bool Enabled = false;
QtMessageHandler PreviousHandler = nullptr;

QMutex CrumbMutex;
QJsonArray Crumbs;
constexpr int kMaxCrumbs = 30;

QString DsnString;

// The DSN is never committed to the repo. It is resolved at init() from,
// in priority order:
//   1. the OCDESKTOP_SENTRY_DSN environment variable at runtime, or
//   2. a compile-time OCDESKTOP_SENTRY_DSN define (the build reads it from
//      the environment / a gitignored .env at configure time — see
//      Telegram/CMakeLists.txt).
// When neither is present the reporter simply stays disabled.
QString resolveDsn() {
	auto fromEnv = qEnvironmentVariable("OCDESKTOP_SENTRY_DSN");
	if (!fromEnv.isEmpty()) {
		return fromEnv;
	}
#ifdef OCDESKTOP_SENTRY_DSN
	return QStringLiteral(OCDESKTOP_SENTRY_DSN);
#else
	return QString();
#endif
}

QString nowIso() {
	return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString newEventId() {
	return QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-');
}

void post(const QJsonObject &event) {
	if (!Enabled || !Manager) {
		return;
	}
	const auto eventId = event.value("event_id").toString();

	QJsonObject header;
	header["event_id"] = eventId;
	header["sent_at"] = nowIso();
	header["dsn"] = DsnString;

	QJsonObject itemHeader;
	itemHeader["type"] = QStringLiteral("event");

	auto envelope = QJsonDocument(header).toJson(QJsonDocument::Compact);
	envelope += "\n";
	envelope += QJsonDocument(itemHeader).toJson(QJsonDocument::Compact);
	envelope += "\n";
	envelope += QJsonDocument(event).toJson(QJsonDocument::Compact);
	envelope += "\n";

	QNetworkRequest req{QUrl(IngestUrl)};
	req.setHeader(QNetworkRequest::ContentTypeHeader,
		QStringLiteral("application/x-sentry-envelope"));
	req.setRawHeader("User-Agent", "ocdesktop-sentry/1.0");

	auto reply = Manager->post(req, envelope);
	QObject::connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}

QJsonObject baseEvent(const QString &level) {
	QJsonObject ev;
	ev["event_id"] = newEventId();
	ev["timestamp"] = nowIso();
	ev["platform"] = QStringLiteral("native");
	ev["level"] = level;
	ev["release"] = Release;
	ev["environment"] = Environment;

	QJsonObject tags;
	tags["os"] = QSysInfo::prettyProductName();
	tags["kernel"] = QSysInfo::kernelVersion();
	tags["arch"] = QSysInfo::currentCpuArchitecture();
	ev["tags"] = tags;

	QMutexLocker lock(&CrumbMutex);
	if (!Crumbs.isEmpty()) {
		QJsonObject crumbs;
		crumbs["values"] = Crumbs;
		ev["breadcrumbs"] = crumbs;
	}
	return ev;
}

void messageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg) {
	const char *level =
		(type == QtFatalMsg) ? "fatal"
		: (type == QtCriticalMsg) ? "error"
		: (type == QtWarningMsg) ? "warning"
		: "info";

	addBreadcrumb(QStringLiteral("qt"), msg);

	if (type == QtFatalMsg || type == QtCriticalMsg) {
		auto ev = baseEvent(QString::fromLatin1(level));
		QJsonObject message;
		message["formatted"] = msg;
		ev["message"] = message;
		QJsonObject logger;
		logger["logger"] = QStringLiteral("qt");
		ev["extra"] = logger;
		post(ev);
	}

	if (PreviousHandler) {
		PreviousHandler(type, ctx, msg);
	}
}

} // namespace

void init(const QString &release, const QString &environment) {
	if (Enabled) {
		return;
	}
	DsnString = resolveDsn();
	if (DsnString.isEmpty()) {
		return; // No DSN configured — reporter stays disabled.
	}

	// DSN format: https://<publicKey>@<ingestHost>/<projectId>
	const QUrl parsed(DsnString);
	const auto key = parsed.userName();
	const auto host = parsed.host();
	const auto project = parsed.path().mid(1);
	if (key.isEmpty() || host.isEmpty() || project.isEmpty()) {
		return; // Malformed DSN — stay disabled rather than post garbage.
	}

	Release = release;
	Environment = environment;
	IngestUrl = QStringLiteral("https://") + host
		+ QStringLiteral("/api/") + project
		+ QStringLiteral("/envelope/?sentry_key=") + key
		+ QStringLiteral("&sentry_version=7");

	Manager = new QNetworkAccessManager();
	Enabled = true;

	PreviousHandler = qInstallMessageHandler(messageHandler);
	addBreadcrumb(QStringLiteral("lifecycle"), QStringLiteral("sentry init"));
}

void addBreadcrumb(const QString &category, const QString &message) {
	QMutexLocker lock(&CrumbMutex);
	QJsonObject c;
	c["timestamp"] = nowIso();
	c["category"] = category;
	c["message"] = message;
	Crumbs.append(c);
	while (Crumbs.size() > kMaxCrumbs) {
		Crumbs.removeFirst();
	}
}

void captureMessage(const QString &level, const QString &message) {
	if (!Enabled) {
		return;
	}
	auto ev = baseEvent(level);
	QJsonObject m;
	m["formatted"] = message;
	ev["message"] = m;
	post(ev);
}

void captureException(const QString &type, const QString &value) {
	if (!Enabled) {
		return;
	}
	auto ev = baseEvent(QStringLiteral("error"));
	QJsonObject single;
	single["type"] = type;
	single["value"] = value;
	QJsonArray values;
	values.append(single);
	QJsonObject exception;
	exception["values"] = values;
	ev["exception"] = exception;
	post(ev);
}

bool isEnabled() {
	return Enabled;
}

} // namespace OpenClaw::Sentry
