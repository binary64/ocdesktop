/*
This file is part of OCDesktop,
a native OpenClaw desktop client forked from Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/binary64/ocdesktop/blob/main/LICENSE
*/
#include "openclaw/MockGateway.h"

#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcessEnvironment>

namespace OpenClaw {
namespace {

[[nodiscard]] QByteArray ReadMockJson() {
	// Resolution order:
	//   1. $OCDESKTOP_MOCK_JSON (explicit override — lets you swap fixtures
	//      without rebuilding).
	//   2. <AppData>/openclaw/mock.json (user-droppable).
	//   3. :/openclaw/mock.json (bundled Qt resource, always present).
	const auto env = QProcessEnvironment::systemEnvironment();
	const auto override = env.value("OCDESKTOP_MOCK_JSON");
	if (!override.isEmpty()) {
		QFile f(override);
		if (f.open(QIODevice::ReadOnly)) {
			return f.readAll();
		}
	}
	const auto userPath = QDir(QStandardPaths::writableLocation(
		QStandardPaths::AppDataLocation)).filePath("openclaw/mock.json");
	{
		QFile f(userPath);
		if (f.open(QIODevice::ReadOnly)) {
			return f.readAll();
		}
	}
	QFile bundled(":/openclaw/mock.json");
	if (bundled.open(QIODevice::ReadOnly)) {
		return bundled.readAll();
	}
	return QByteArray();
}

} // namespace

MockGateway::MockGateway() {
	seedFixtures();
}

void MockGateway::seedFixtures() {
	const auto raw = ReadMockJson();
	if (raw.isEmpty()) {
		return;
	}
	const auto doc = QJsonDocument::fromJson(raw);
	if (!doc.isObject()) {
		return;
	}
	const auto root = doc.object();

	for (const auto &v : root.value("peers").toArray()) {
		const auto o = v.toObject();
		GatewayPeer peer;
		peer.id = PeerId(o.value("id").toVariant().toULongLong());
		peer.name = o.value("name").toString();
		peer.username = o.value("username").toString();
		peer.about = o.value("about").toString();
		peer.isBot = o.value("isBot").toBool();
		peer.isGroup = o.value("isGroup").toBool();
		peer.isChannel = o.value("isChannel").toBool();
		_peers.emplace(peer.id, peer);
	}

	for (const auto &v : root.value("dialogs").toArray()) {
		const auto o = v.toObject();
		GatewayDialog dialog;
		dialog.peerId = PeerId(o.value("peerId").toVariant().toULongLong());
		dialog.isPinned = o.value("pinned").toBool();
		dialog.isArchived = o.value("archived").toBool();
		dialog.unreadCount = o.value("unreadCount").toInt();
		if (const auto it = _peers.find(dialog.peerId); it != _peers.end()) {
			dialog.title = it->second.name;
		}
		_dialogs.push_back(dialog);
	}

	const auto historyObj = root.value("history").toObject();
	for (auto it = historyObj.begin(); it != historyObj.end(); ++it) {
		const auto peerId = PeerId(it.key().toULongLong());
		std::vector<GatewayMessage> messages;
		for (const auto &v : it.value().toArray()) {
			const auto o = v.toObject();
			GatewayMessage msg;
			msg.id = MsgId(o.value("id").toVariant().toLongLong());
			msg.peerId = peerId;
			msg.fromId = PeerId(o.value("fromId").toVariant().toULongLong());
			msg.text = o.value("text").toString();
			msg.date = TimeId(o.value("date").toVariant().toLongLong());
			msg.replyToMsgId = MsgId(o.value("replyToMsgId").toVariant().toLongLong());
			if (msg.id >= _nextMsgId) {
				_nextMsgId = msg.id + 1;
			}
			messages.push_back(msg);
		}
		if (const auto d = std::find_if(_dialogs.begin(), _dialogs.end(),
				[&](const GatewayDialog &x) { return x.peerId == peerId; });
			d != _dialogs.end() && !messages.empty()) {
			d->topMessageId = messages.back().id;
		}
		_history.emplace(peerId, std::move(messages));
	}
}

// --- Auth ---
AuthState MockGateway::authState() const {
	return _authState;
}

void MockGateway::logout(DoneCallback done) {
	_authState = AuthState::LoggedOut;
	if (done) done();
}

// --- Dialogs ---
void MockGateway::requestDialogs(
		bool archived,
		GatewayCallback<std::vector<GatewayDialog>> done,
		ErrorCallback fail) {
	std::vector<GatewayDialog> out;
	for (const auto &d : _dialogs) {
		if (d.isArchived == archived) {
			out.push_back(d);
		}
	}
	if (done) done(std::move(out));
}

void MockGateway::requestPinnedDialogs(
		bool archived,
		GatewayCallback<std::vector<GatewayDialog>> done,
		ErrorCallback fail) {
	std::vector<GatewayDialog> out;
	for (const auto &d : _dialogs) {
		if (d.isArchived == archived && d.isPinned) {
			out.push_back(d);
		}
	}
	if (done) done(std::move(out));
}

void MockGateway::savePinnedOrder(
		const std::vector<PeerId> &order,
		bool archived,
		DoneCallback done,
		ErrorCallback fail) {
	if (done) done();
}

// --- Messages ---
void MockGateway::sendMessage(
		PeerId peerId,
		const QString &text,
		MsgId replyTo,
		GatewayCallback<GatewayMessage> done,
		ErrorCallback fail) {
	// Locally echo the user's own outgoing message into history so the UI
	// reflects it. No backend reply is generated (by design, Phase 1).
	GatewayMessage msg;
	msg.id = _nextMsgId++;
	msg.peerId = peerId;
	msg.fromId = PeerId(1); // self
	msg.text = text;
	msg.replyToMsgId = replyTo;
	_history[peerId].push_back(msg);
	if (done) done(msg);
}

void MockGateway::requestHistory(
		PeerId peerId,
		MsgId offsetId,
		int limit,
		GatewayCallback<std::vector<GatewayMessage>> done,
		ErrorCallback fail) {
	std::vector<GatewayMessage> out;
	if (const auto it = _history.find(peerId); it != _history.end()) {
		out = it->second;
	}
	if (done) done(std::move(out));
}

void MockGateway::requestMessageData(
		PeerId peerId,
		MsgId msgId,
		GatewayCallback<GatewayMessage> done,
		ErrorCallback fail) {
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

void MockGateway::deleteMessages(
		PeerId peerId,
		const std::vector<MsgId> &ids,
		bool revoke,
		DoneCallback done,
		ErrorCallback fail) {
	if (const auto it = _history.find(peerId); it != _history.end()) {
		auto &msgs = it->second;
		msgs.erase(std::remove_if(msgs.begin(), msgs.end(),
			[&](const GatewayMessage &m) {
				return std::find(ids.begin(), ids.end(), m.id) != ids.end();
			}), msgs.end());
	}
	if (done) done();
}

void MockGateway::markContentsRead(
		PeerId peerId,
		const std::vector<MsgId> &ids,
		DoneCallback done) {
	if (done) done();
}

// --- Peer Info ---
void MockGateway::requestFullPeer(
		PeerId peerId,
		GatewayCallback<GatewayPeer> done,
		ErrorCallback fail) {
	if (const auto it = _peers.find(peerId); it != _peers.end()) {
		if (done) done(it->second);
		return;
	}
	if (fail) fail("peer not found");
}

void MockGateway::requestContacts(
		GatewayCallback<std::vector<GatewayPeer>> done,
		ErrorCallback fail) {
	std::vector<GatewayPeer> out;
	for (const auto &[id, peer] : _peers) {
		out.push_back(peer);
	}
	if (done) done(std::move(out));
}

// --- Presence & Typing ---
void MockGateway::sendTyping(PeerId peerId, bool typing) {
	// no-op
}

// --- File Transfer ---
void MockGateway::uploadFile(
		const FileRequest &request,
		GatewayCallback<FileResult> done,
		ErrorCallback fail) {
	FileResult result;
	result.localPath = request.localPath;
	result.success = true;
	if (done) done(result);
}

void MockGateway::downloadFile(
		const FileRequest &request,
		GatewayCallback<FileResult> done,
		ErrorCallback fail) {
	FileResult result;
	result.localPath = request.localPath;
	result.success = true;
	if (done) done(result);
}

// --- Updates Stream ---
void MockGateway::setUpdateHandler(UpdateHandler handler) {
	_updateHandler = std::move(handler);
}

// --- Search ---
void MockGateway::searchMessages(
		PeerId peerId,
		const QString &query,
		int limit,
		GatewayCallback<std::vector<GatewayMessage>> done,
		ErrorCallback fail) {
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
