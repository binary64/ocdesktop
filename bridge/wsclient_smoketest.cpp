#include "openclaw/WsClient.h"
#include <QCoreApplication>
#include <QTimer>
#include <QFile>
#include <cstdio>

using namespace OpenClaw;

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);

	QString token;
	QFile f(QStringLiteral("/work/ocdesktop-ws.env"));
	if (f.open(QIODevice::ReadOnly)) {
		for (const auto &line : QString::fromUtf8(f.readAll()).split('\n')) {
			if (line.startsWith("OCDESKTOP_WS_TOKEN=")) {
				token = line.mid(QStringLiteral("OCDESKTOP_WS_TOKEN=").size()).trimmed();
			}
		}
	}

	auto *ws = new WsClient(&app);
	int step = 0;

	QObject::connect(ws, &WsClient::connected, [&]() {
		std::printf("CONNECTED\n");
		ws->sendText(QStringLiteral("{\"id\":1,\"op\":\"auth\",\"token\":\"%1\"}").arg(token));
	});
	QObject::connect(ws, &WsClient::textMessage, [&](const QString &msg) {
		std::printf("RECV: %s\n", msg.left(120).toUtf8().constData());
		std::fflush(stdout);
		if (step == 0) {
			step = 1;
			ws->sendText(QStringLiteral("{\"id\":2,\"op\":\"sessions.list\"}"));
		} else if (step == 1) {
			step = 2;
			if (msg.contains("\"peers\"")) {
				std::printf("RESULT: sessions.list returned peers — FRAMING OK\n");
			}
			ws->close();
			QTimer::singleShot(100, &app, [&]() { app.exit(0); });
		}
	});
	QObject::connect(ws, &WsClient::disconnected, [&](const QString &reason) {
		std::printf("DISCONNECTED: %s\n", reason.toUtf8().constData());
	});

	ws->open(QStringLiteral("ws://100.99.160.15:8770/ocdesktop"));
	QTimer::singleShot(15000, &app, [&]() { std::printf("TIMEOUT\n"); app.exit(2); });
	return app.exec();
}
