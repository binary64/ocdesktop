/*
This file is part of OCDesktop,
a native OpenClaw desktop client forked from Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/binary64/ocdesktop/blob/main/LICENSE
*/
#include "openclaw/WsClient.h"

#include <QSslSocket>
#include <QSslError>
#include <QList>
#include <QStringList>
#include <QCryptographicHash>
#include <QUrl>
#include <QRandomGenerator>
#include <QTimer>
#include <iterator>

namespace OpenClaw {
namespace {

constexpr quint8 kOpText = 0x1;
constexpr quint8 kOpClose = 0x8;
constexpr quint8 kOpPing = 0x9;
constexpr quint8 kOpPong = 0xA;
const auto kGuid = QByteArrayLiteral("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");

} // namespace

WsClient::WsClient(QObject *parent) : QObject(parent) {
}

WsClient::~WsClient() {
	if (_socket) {
		_socket->disconnect(this);
		_socket->abort();
	}
}

void WsClient::open(const QString &url) {
	_url = url;
	_deliberateClose = false;
	const auto parsed = QUrl(url);
	_host = parsed.host();
	_path = parsed.path().isEmpty() ? QStringLiteral("/") : parsed.path();
	_secure = (parsed.scheme().compare(QStringLiteral("wss"), Qt::CaseInsensitive) == 0);
	_port = parsed.port(_secure ? 443 : 80);

	if (_socket) {
		_socket->disconnect(this);
		_socket->abort();
		_socket->deleteLater();
		_socket = nullptr;
	}
	_buffer.clear();

	_socket = new QSslSocket(this);
	connect(_socket, &QSslSocket::readyRead, this, &WsClient::onReadyRead);
	connect(_socket, &QSslSocket::disconnected, this, &WsClient::onDisconnected);
	connect(_socket, &QAbstractSocket::errorOccurred, this, [=](QAbstractSocket::SocketError) {
		fail(_socket->errorString());
	});
	connect(_socket, &QSslSocket::sslErrors, this, [=](const QList<QSslError> &errors) {
		QStringList parts;
		for (const auto &e : errors) {
			parts.push_back(e.errorString());
		}
		fail(QStringLiteral("TLS: ") + parts.join(QStringLiteral("; ")));
	});

	_state = State::Connecting;
	if (_secure) {
		connect(_socket, &QSslSocket::encrypted, this, &WsClient::onConnected);
		_socket->connectToHostEncrypted(_host, _port);
	} else {
		connect(_socket, &QSslSocket::connected, this, &WsClient::onConnected);
		_socket->connectToHost(_host, _port);
	}
}

void WsClient::onConnected() {
	_state = State::Handshake;

	QByteArray rawKey(16, Qt::Uninitialized);
	for (int i = 0; i < 16; ++i) {
		rawKey[i] = char(QRandomGenerator::global()->bounded(256));
	}
	const auto key = rawKey.toBase64();

	auto accept = QCryptographicHash::hash(key + kGuid, QCryptographicHash::Sha1);
	_expectedAccept = accept.toBase64();

	QByteArray req;
	const bool defaultPort = (_secure && _port == 443) || (!_secure && _port == 80);
	const auto hostHeader = defaultPort
		? _host.toUtf8()
		: _host.toUtf8() + ":" + QByteArray::number(_port);
	req += "GET " + _path.toUtf8() + " HTTP/1.1\r\n";
	req += "Host: " + hostHeader + "\r\n";
	req += "Upgrade: websocket\r\n";
	req += "Connection: Upgrade\r\n";
	req += "Sec-WebSocket-Key: " + key + "\r\n";
	req += "Sec-WebSocket-Version: 13\r\n\r\n";
	_socket->write(req);
}

void WsClient::onReadyRead() {
	_buffer += _socket->readAll();
	if (_state == State::Handshake) {
		processHandshake();
	}
	if (_state == State::Open) {
		processFrames();
	}
}

void WsClient::processHandshake() {
	const auto headerEnd = _buffer.indexOf("\r\n\r\n");
	if (headerEnd < 0) {
		return;
	}
	const auto header = _buffer.left(headerEnd);
	_buffer = _buffer.mid(headerEnd + 4);

	if (!header.contains("101") || !header.contains(_expectedAccept)) {
		fail(QStringLiteral("handshake rejected"));
		return;
	}
	_state = State::Open;
	_reconnectAttempts = 0;
	Q_EMIT connected();
}

void WsClient::processFrames() {
	for (;;) {
		if (_buffer.size() < 2) {
			return;
		}
		const auto b0 = quint8(_buffer[0]);
		const auto b1 = quint8(_buffer[1]);
		const bool fin = (b0 & 0x80) != 0;
		const quint8 opcode = b0 & 0x0F;
		const bool masked = (b1 & 0x80) != 0;
		quint64 len = b1 & 0x7F;
		int offset = 2;

		if (len == 126) {
			if (_buffer.size() < offset + 2) return;
			len = (quint8(_buffer[offset]) << 8) | quint8(_buffer[offset + 1]);
			offset += 2;
		} else if (len == 127) {
			if (_buffer.size() < offset + 8) return;
			len = 0;
			for (int i = 0; i < 8; ++i) {
				len = (len << 8) | quint8(_buffer[offset + i]);
			}
			offset += 8;
		}

		QByteArray maskKey;
		if (masked) {
			if (_buffer.size() < offset + 4) return;
			maskKey = _buffer.mid(offset, 4);
			offset += 4;
		}

		if (quint64(_buffer.size()) < quint64(offset) + len) {
			return;
		}

		QByteArray payload = _buffer.mid(offset, int(len));
		if (masked) {
			for (int i = 0; i < payload.size(); ++i) {
				payload[i] = payload[i] ^ maskKey[i % 4];
			}
		}
		_buffer = _buffer.mid(offset + int(len));

		if (opcode == kOpText && fin) {
			Q_EMIT textMessage(QString::fromUtf8(payload));
		} else if (opcode == kOpPing) {
			sendFrame(kOpPong, payload);
		} else if (opcode == kOpClose) {
			fail(QStringLiteral("server closed"));
			return;
		}
	}
}

void WsClient::sendText(const QString &text) {
	if (_state != State::Open) {
		return;
	}
	sendFrame(kOpText, text.toUtf8());
}

void WsClient::sendFrame(quint8 opcode, const QByteArray &payload) {
	QByteArray frame;
	frame.append(char(0x80 | opcode));

	const auto len = payload.size();
	if (len < 126) {
		frame.append(char(0x80 | len));
	} else if (len <= 0xFFFF) {
		frame.append(char(0x80 | 126));
		frame.append(char((len >> 8) & 0xFF));
		frame.append(char(len & 0xFF));
	} else {
		frame.append(char(0x80 | 127));
		for (int i = 7; i >= 0; --i) {
			frame.append(char((quint64(len) >> (i * 8)) & 0xFF));
		}
	}

	char mask[4];
	for (int i = 0; i < 4; ++i) {
		mask[i] = char(QRandomGenerator::global()->bounded(256));
	}
	frame.append(mask, 4);

	QByteArray masked = payload;
	for (int i = 0; i < masked.size(); ++i) {
		masked[i] = masked[i] ^ mask[i % 4];
	}
	frame.append(masked);
	_socket->write(frame);
}

void WsClient::close() {
	_deliberateClose = true;
	if (_reconnectTimer) {
		_reconnectTimer->stop();
	}
	if (_socket && _state == State::Open) {
		sendFrame(kOpClose, QByteArray());
	}
	_state = State::Closed;
	if (_socket) {
		_socket->close();
	}
}

void WsClient::onDisconnected() {
	if (_state != State::Closed) {
		fail(QStringLiteral("disconnected"));
	}
}

void WsClient::scheduleReconnect() {
	if (_deliberateClose || _url.isEmpty()) {
		return;
	}
	if (!_reconnectTimer) {
		_reconnectTimer = new QTimer(this);
		_reconnectTimer->setSingleShot(true);
		connect(_reconnectTimer, &QTimer::timeout, this, [this] {
			if (!_deliberateClose) {
				open(_url);
			}
		});
	}
	const int caps[] = { 1000, 2000, 5000, 10000, 15000, 30000 };
	const int idx = qMin(_reconnectAttempts, int(std::size(caps)) - 1);
	const int delay = caps[idx];
	++_reconnectAttempts;
	_reconnectTimer->start(delay);
}

void WsClient::fail(const QString &reason) {
	if (_state == State::Closed) {
		return;
	}
	_state = State::Closed;
	Q_EMIT disconnected(reason);
	scheduleReconnect();
}

} // namespace OpenClaw
