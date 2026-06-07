/*
This file is part of OCDesktop,
a native OpenClaw desktop client forked from Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/binary64/ocdesktop/blob/main/LICENSE
*/
#include "openclaw/WsGateway.h"
#include "openclaw/SentryReporter.h"
#include "openclaw/BrowserSection.h"

#include <crl/crl_on_main.h>

#include <QJsonDocument>
#include <QJsonArray>
#include <QEventLoop>
#include <QTimer>
#include <QElapsedTimer>

namespace OpenClaw {

WsGateway::WsGateway(const QString &url, const QString &token, const QString &user, QObject *parent)
: QObject(parent)
, _url(url)
, _token(token)
, _user(user) {
	_ws = new WsClient(this);
	connect(_ws, &WsClient::connected, this, &WsGateway::onConnected);
	connect(_ws, &WsClient::textMessage, this, &WsGateway::onTextMessage);
	connect(_ws, &WsClient::disconnected, this, [=](const QString &reason) {
		const bool wasReady = (_authState == AuthState::Ready);
		_authState = AuthState::LoggedOut;
		Sentry::addBreadcrumb(QStringLiteral("ws"),
			QStringLiteral("disconnected: ") + reason);
		if (wasReady) {
			Sentry::captureException(
				QStringLiteral("WsDisconnected"),
				QStringLiteral("gateway dropped while ready: ") + reason);
		}
	});
}

WsGateway::~WsGateway() = default;

int WsGateway::nextRequestId() {
	return ++_requestId;
}

void WsGateway::send(const QJsonObject &frame) {
	_ws->sendText(QString::fromUtf8(QJsonDocument(frame).toJson(QJsonDocument::Compact)));
}

void WsGateway::onConnected() {
	auto frame = QJsonObject{
		{ "id", nextRequestId() },
		{ "op", "auth" },
		{ "token", _token },
	};
	if (!_user.isEmpty()) {
		frame.insert("user", _user);
	}
	send(frame);
}

bool WsGateway::bootstrapBlocking(int timeoutMs) {
	QEventLoop loop;
	QElapsedTimer clock;
	clock.start();

	QTimer poll;
	poll.setInterval(50);
	connect(&poll, &QTimer::timeout, &loop, [&]() {
		if (_authed && _gotSessions && _pendingHistories == 0) {
			loop.quit();
		} else if (clock.elapsed() > timeoutMs) {
			loop.quit();
		} else if (_authState == AuthState::LoggedOut && clock.elapsed() > 1000) {
			loop.quit();
		}
	});
	poll.start();

	_ws->open(_url);
	loop.exec();

	return _authed && _gotSessions;
}

bool WsGateway::fetchRosterBlocking(int timeoutMs) {
	_rosterOnly = true;
	QEventLoop loop;
	QElapsedTimer clock;
	clock.start();

	QTimer poll;
	poll.setInterval(50);
	connect(&poll, &QTimer::timeout, &loop, [&]() {
		if (_authed && _gotRoster) {
			loop.quit();
		} else if (clock.elapsed() > timeoutMs) {
			loop.quit();
		} else if (_authState == AuthState::LoggedOut && clock.elapsed() > 1000) {
			loop.quit();
		}
	});
	poll.start();

	_ws->open(_url);
	loop.exec();

	return _authed && _gotRoster;
}

void WsGateway::onTextMessage(const QString &raw) {
	const auto doc = QJsonDocument::fromJson(raw.toUtf8());
	if (!doc.isObject()) {
		return;
	}
	const auto obj = doc.object();
	if (obj.value("op").toString() == "update") {
		handleUpdate(obj);
		return;
	}
	handleResponse(obj.value("id").toInt(), obj);
}

void WsGateway::handleResponse(int id, const QJsonObject &obj) {
	const bool ok = obj.value("ok").toBool();
	const auto result = obj.value("result").toObject();

	if (!_authed && result.contains("self")) {
		_authed = ok;
		_authState = ok ? AuthState::Ready : AuthState::LoggedOut;
		if (ok) {
			Sentry::addBreadcrumb(QStringLiteral("ws"), QStringLiteral("auth ok"));
			if (_rosterOnly) {
				send(QJsonObject{ { "id", nextRequestId() }, { "op", "roster" } });
			} else {
				send(QJsonObject{ { "id", nextRequestId() }, { "op", "sessions.list" } });
			}
		} else {
			Sentry::captureException(
				QStringLiteral("WsAuthFailed"),
				QStringLiteral("gateway auth rejected: ")
					+ obj.value("error").toString());
		}
		return;
	}

	if (result.contains("users") && !result.contains("dialogs")) {
		_roster.clear();
		for (const auto &v : result.value("users").toArray()) {
			const auto o = v.toObject();
			KnownUser u;
			u.id = o.value("id").toVariant().toString();
			u.label = o.value("label").toString();
			if (u.label.isEmpty()) {
				u.label = u.id;
			}
			if (!u.id.isEmpty()) {
				_roster.push_back(u);
			}
		}
		_gotRoster = true;
		return;
	}

	if (result.contains("peers") && result.contains("dialogs")) {
		_peers.clear();
		_dialogs.clear();
		for (const auto &v : result.value("peers").toArray()) {
			const auto o = v.toObject();
			GatewayPeer peer;
			peer.id = PeerId(o.value("id").toVariant().toULongLong());
			peer.name = o.value("name").toString();
			peer.username = o.value("username").toString();
			peer.about = o.value("about").toString();
			peer.isBot = o.value("isBot").toBool();
			_peers.emplace(peer.id, peer);
		}
		for (const auto &v : result.value("dialogs").toArray()) {
			const auto o = v.toObject();
			GatewayDialog dialog;
			dialog.peerId = PeerId(o.value("peerId").toVariant().toULongLong());
			dialog.isPinned = o.value("pinned").toBool();
			dialog.isArchived = o.value("archived").toBool();
			dialog.unreadCount = o.value("unreadCount").toInt();
			dialog.topMessageId = MsgId(o.value("topMessageId").toVariant().toLongLong());
			if (const auto it = _peers.find(dialog.peerId); it != _peers.end()) {
				dialog.title = it->second.name;
			}
			_dialogs.push_back(dialog);
		}
		_gotSessions = true;
		_pendingHistories = int(_dialogs.size());
		for (const auto &d : _dialogs) {
			const auto rid = nextRequestId();
			_historyCallbacks.emplace(rid, std::make_pair(d.peerId, GatewayCallback<std::vector<GatewayMessage>>()));
			send(QJsonObject{
				{ "id", rid },
				{ "op", "history" },
				{ "peerId", QJsonValue::fromVariant(QVariant::fromValue<qulonglong>(d.peerId)) },
				{ "limit", 50 },
			});
		}
		return;
	}

	if (const auto it = _historyCallbacks.find(id); it != _historyCallbacks.end()) {
		const auto peerId = it->second.first;
		auto userCb = it->second.second;
		std::vector<GatewayMessage> msgs;
		for (const auto &v : result.value("messages").toArray()) {
			const auto o = v.toObject();
			GatewayMessage msg;
			msg.id = MsgId(o.value("id").toVariant().toLongLong());
			msg.peerId = peerId;
			msg.fromId = PeerId(o.value("fromId").toVariant().toULongLong());
			msg.text = o.value("text").toString();
			msg.date = TimeId(o.value("date").toVariant().toLongLong());
			msgs.push_back(msg);
		}
		_history[peerId] = msgs;
		const auto lastUrl = result.value("lastBrowserUrl").toString();
		if (!lastUrl.isEmpty()) {
			OpenClaw::BrowserSection::RememberUrl(
				uint64(peerId),
				lastUrl);
		}
		if (_pendingHistories > 0) {
			--_pendingHistories;
		}
		_historyCallbacks.erase(it);
		if (userCb) {
			userCb(std::move(msgs));
		}
		return;
	}

	if (const auto it = _sendCallbacks.find(id); it != _sendCallbacks.end()) {
		auto cb = it->second;
		_sendCallbacks.erase(it);
		const auto m = result.value("message").toObject();
		GatewayMessage msg;
		msg.id = MsgId(m.value("id").toVariant().toLongLong());
		msg.peerId = PeerId(m.value("peerId").toVariant().toULongLong());
		msg.fromId = PeerId(m.value("fromId").toVariant().toULongLong());
		msg.text = m.value("text").toString();
		msg.date = TimeId(m.value("date").toVariant().toLongLong());
		_history[msg.peerId].push_back(msg);
		if (cb) cb(msg);
		return;
	}
}

void WsGateway::handleUpdate(const QJsonObject &obj) {
	const auto kind = obj.value("kind").toString();
	const auto peerId = PeerId(obj.value("peerId").toVariant().toULongLong());

	if (kind == "browser.navigate") {
		const auto url = obj.value("url").toString();
		crl::on_main([peerId, url] {
			OpenClaw::BrowserSection::NavigatePeer(uint64(peerId), url);
		});
		return;
	}

	if (kind == "stream.delta" || kind == "stream.start") {
		const auto msgId = MsgId(obj.value("msgId").toVariant().toLongLong());
		GatewayMessage msg;
		msg.id = msgId;
		msg.peerId = peerId;
		msg.fromId = peerId;
		msg.text = obj.value("text").toString();
		if (_updateHandler) {
			_updateHandler(msg);
		}
	} else if (kind == "message.new") {
		const auto m = obj.value("message").toObject();
		GatewayMessage msg;
		msg.id = MsgId(m.value("id").toVariant().toLongLong());
		msg.peerId = peerId;
		msg.fromId = PeerId(m.value("fromId").toVariant().toULongLong());
		msg.text = m.value("text").toString();
		msg.date = TimeId(m.value("date").toVariant().toLongLong());
		_history[peerId].push_back(msg);
		if (_updateHandler) {
			_updateHandler(msg);
		}
	}
}

AuthState WsGateway::authState() const {
	return _authState;
}

void WsGateway::logout(DoneCallback done) {
	_ws->close();
	_authState = AuthState::LoggedOut;
	if (done) done();
}

void WsGateway::requestDialogs(bool archived, GatewayCallback<std::vector<GatewayDialog>> done, ErrorCallback fail) {
	std::vector<GatewayDialog> out;
	for (const auto &d : _dialogs) {
		if (d.isArchived == archived) {
			out.push_back(d);
		}
	}
	if (done) done(std::move(out));
}

void WsGateway::requestPinnedDialogs(bool archived, GatewayCallback<std::vector<GatewayDialog>> done, ErrorCallback fail) {
	std::vector<GatewayDialog> out;
	for (const auto &d : _dialogs) {
		if (d.isArchived == archived && d.isPinned) {
			out.push_back(d);
		}
	}
	if (done) done(std::move(out));
}

void WsGateway::savePinnedOrder(const std::vector<PeerId> &order, bool archived, DoneCallback done, ErrorCallback fail) {
	if (done) done();
}

void WsGateway::sendMessage(PeerId peerId, const QString &text, MsgId replyTo, GatewayCallback<GatewayMessage> done, ErrorCallback fail) {
	const auto rid = nextRequestId();
	if (done) {
		_sendCallbacks.emplace(rid, done);
	}
	send(QJsonObject{
		{ "id", rid },
		{ "op", "message.send" },
		{ "peerId", QJsonValue::fromVariant(QVariant::fromValue<qulonglong>(peerId)) },
		{ "text", text },
		{ "replyTo", QJsonValue::fromVariant(QVariant::fromValue<qlonglong>(replyTo)) },
	});
}

void WsGateway::requestHistory(PeerId peerId, MsgId offsetId, int limit, GatewayCallback<std::vector<GatewayMessage>> done, ErrorCallback fail) {
	if (const auto it = _history.find(peerId); it != _history.end()) {
		if (done) done(it->second);
		return;
	}
	const auto rid = nextRequestId();
	_historyCallbacks.emplace(rid, std::make_pair(peerId, done));
	send(QJsonObject{
		{ "id", rid },
		{ "op", "history" },
		{ "peerId", QJsonValue::fromVariant(QVariant::fromValue<qulonglong>(peerId)) },
		{ "limit", limit },
	});
}

void WsGateway::requestMessageData(PeerId peerId, MsgId msgId, GatewayCallback<GatewayMessage> done, ErrorCallback fail) {
	if (const auto it = _history.find(peerId); it != _history.end()) {
		for (const auto &m : it->second) {
			if (m.id == msgId) {
				if (done) done(m);
				return;
			}
		}
	}
	if (fail) fail("message not found");
}

void WsGateway::deleteMessages(PeerId peerId, const std::vector<MsgId> &ids, bool revoke, DoneCallback done, ErrorCallback fail) {
	if (done) done();
}

void WsGateway::markContentsRead(PeerId peerId, const std::vector<MsgId> &ids, DoneCallback done) {
	if (done) done();
}

void WsGateway::requestFullPeer(PeerId peerId, GatewayCallback<GatewayPeer> done, ErrorCallback fail) {
	if (const auto it = _peers.find(peerId); it != _peers.end()) {
		if (done) done(it->second);
		return;
	}
	if (fail) fail("peer not found");
}

void WsGateway::requestContacts(GatewayCallback<std::vector<GatewayPeer>> done, ErrorCallback fail) {
	std::vector<GatewayPeer> out;
	for (const auto &[id, peer] : _peers) {
		out.push_back(peer);
	}
	if (done) done(std::move(out));
}

void WsGateway::sendTyping(PeerId peerId, bool typing) {
	send(QJsonObject{
		{ "id", nextRequestId() },
		{ "op", "typing" },
		{ "peerId", QJsonValue::fromVariant(QVariant::fromValue<qulonglong>(peerId)) },
		{ "typing", typing },
	});
}

void WsGateway::uploadFile(const FileRequest &request, GatewayCallback<FileResult> done, ErrorCallback fail) {
	FileResult result;
	result.localPath = request.localPath;
	result.success = false;
	result.error = "file transfer not supported";
	if (fail) fail(result.error);
}

void WsGateway::downloadFile(const FileRequest &request, GatewayCallback<FileResult> done, ErrorCallback fail) {
	FileResult result;
	result.localPath = request.localPath;
	result.success = false;
	result.error = "file transfer not supported";
	if (fail) fail(result.error);
}

void WsGateway::setUpdateHandler(UpdateHandler handler) {
	_updateHandler = std::move(handler);
}

void WsGateway::searchMessages(PeerId peerId, const QString &query, int limit, GatewayCallback<std::vector<GatewayMessage>> done, ErrorCallback fail) {
	std::vector<GatewayMessage> out;
	const auto scan = [&](const std::vector<GatewayMessage> &msgs) {
		for (const auto &m : msgs) {
			if (m.text.contains(query, Qt::CaseInsensitive)) {
				out.push_back(m);
				if (int(out.size()) >= limit) break;
			}
		}
	};
	if (peerId) {
		if (const auto it = _history.find(peerId); it != _history.end()) {
			scan(it->second);
		}
	} else {
		for (const auto &[id, msgs] : _history) {
			scan(msgs);
			if (int(out.size()) >= limit) break;
		}
	}
	if (done) done(std::move(out));
}

} // namespace OpenClaw
