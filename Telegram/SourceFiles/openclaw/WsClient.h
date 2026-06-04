/*
This file is part of OCDesktop,
a native OpenClaw desktop client forked from Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/binary64/ocdesktop/blob/main/LICENSE
*/
#pragma once

#include <QObject>
#include <QByteArray>
#include <QString>
#include <functional>

class QTcpSocket;

namespace OpenClaw {

// ============================================================
// WsClient — a minimal RFC 6455 WebSocket text client built on
// QTcpSocket. tdesktop's static Qt build does NOT ship the
// QtWebSockets module, so we implement just the slice we need:
//   - client handshake (ws:// only; Tailscale is the trust layer)
//   - masked text frames out, unmasked text frames in
//   - close + ping/pong
//
// Binary frames, fragmentation beyond simple continuation, and
// permessage-deflate are intentionally unsupported — the bridge
// never uses them.
// ============================================================
class WsClient final : public QObject {
	Q_OBJECT
public:
	explicit WsClient(QObject *parent = nullptr);
	~WsClient() override;

	void open(const QString &url);
	void sendText(const QString &text);
	void close();
	[[nodiscard]] bool isConnected() const { return _state == State::Open; }

Q_SIGNALS:
	void connected();
	void textMessage(const QString &text);
	void disconnected(const QString &reason);

private:
	enum class State { Idle, Connecting, Handshake, Open, Closed };

	void onConnected();
	void onReadyRead();
	void onDisconnected();
	void processHandshake();
	void processFrames();
	void sendFrame(quint8 opcode, const QByteArray &payload);
	void fail(const QString &reason);

	QTcpSocket *_socket = nullptr;
	State _state = State::Idle;
	QString _host;
	QString _path;
	quint16 _port = 80;
	QByteArray _expectedAccept;
	QByteArray _buffer;
};

} // namespace OpenClaw
