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

class QSslSocket;

namespace OpenClaw {

// ============================================================
// WsClient — a minimal RFC 6455 WebSocket text client built on
// QSslSocket. tdesktop's static Qt build does NOT ship the
// QtWebSockets module, so we implement just the slice we need:
//   - client handshake for ws:// (plain) and wss:// (TLS)
//   - masked text frames out, unmasked text frames in
//   - close + ping/pong
//
// QtNetwork's OpenSSL TLS backend IS linked in (MTProto needs
// OpenSSL), so QSslSocket gives us real wss:// with full chain
// validation — used for Tailscale's Let's Encrypt cert. For
// ws:// the same socket runs in unencrypted mode.
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
	void scheduleReconnect();

	QSslSocket *_socket = nullptr;
	bool _secure = false;
	State _state = State::Idle;
	QString _url;
	QString _host;
	QString _path;
	quint16 _port = 80;
	QByteArray _expectedAccept;
	QByteArray _buffer;
	bool _deliberateClose = false;
	int _reconnectAttempts = 0;
	class QTimer *_reconnectTimer = nullptr;
};

} // namespace OpenClaw
