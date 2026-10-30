/*
This file is part of OCDesktop,
a native OpenClaw desktop client forked from Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/binary64/ocdesktop/blob/main/LICENSE
*/
#pragma once

#include "openclaw/GatewayInterface.h"
#include "openclaw/WsClient.h"
#include "openclaw/ConnectConfig.h"

#include <QObject>
#include <QJsonObject>
#include <unordered_map>
#include <map>

namespace OpenClaw {

// ============================================================
// WsGateway — Phase 2 implementation of GatewayInterface.
//
// Speaks docs/HERMES_WS_PROTOCOL.md to the Hermes-side WS bridge
// over WsClient. Each Hermes session is a dialog; each turn a
// message; message.send fires a real agent turn and the reply
// streams back via the update handler.
//
// Mirrors MockGateway's container shape (_dialogs/_peers/_history
// + accessors) so the existing seeder translation works unchanged.
// ============================================================
class WsGateway final : public QObject, public GatewayInterface {
	Q_OBJECT
public:
	WsGateway(const QString &url, const QString &token, const QString &user = QString(), QObject *parent = nullptr);
	~WsGateway() override;

	// Block-wait the Qt event loop until auth + sessions.list + all
	// histories have landed (or timeout). Lets the synchronous seeder
	// pull a fully-populated snapshot. Returns false on failure.
	bool bootstrapBlocking(int timeoutMs = 15000);

	// Lightweight one-shot: auth then fetch the household roster (the bridge's
	// live list of user_ids + display names derived from real session data),
	// WITHOUT pulling sessions/histories. Used to populate the picker before a
	// user is chosen, so the member list is never hardcoded in the client.
	bool fetchRosterBlocking(int timeoutMs = 8000);
	[[nodiscard]] const std::vector<KnownUser> &roster() const { return _roster; }

	// --- Auth ---
	[[nodiscard]] AuthState authState() const override;
	void logout(DoneCallback done) override;

	// --- Dialogs ---
	void requestDialogs(bool archived, GatewayCallback<std::vector<GatewayDialog>> done, ErrorCallback fail) override;
	void requestPinnedDialogs(bool archived, GatewayCallback<std::vector<GatewayDialog>> done, ErrorCallback fail) override;
	void savePinnedOrder(const std::vector<PeerId> &order, bool archived, DoneCallback done, ErrorCallback fail) override;

	// --- Messages ---
	void sendMessage(PeerId peerId, const QString &text, MsgId replyTo, GatewayCallback<GatewayMessage> done, ErrorCallback fail) override;
	void requestHistory(PeerId peerId, MsgId offsetId, int limit, GatewayCallback<std::vector<GatewayMessage>> done, ErrorCallback fail) override;
	void requestMessageData(PeerId peerId, MsgId msgId, GatewayCallback<GatewayMessage> done, ErrorCallback fail) override;
	void deleteMessages(PeerId peerId, const std::vector<MsgId> &ids, bool revoke, DoneCallback done, ErrorCallback fail) override;
	void markContentsRead(PeerId peerId, const std::vector<MsgId> &ids, DoneCallback done) override;

	// --- Peer Info ---
	void requestFullPeer(PeerId peerId, GatewayCallback<GatewayPeer> done, ErrorCallback fail) override;
	void requestContacts(GatewayCallback<std::vector<GatewayPeer>> done, ErrorCallback fail) override;

	// --- Presence & Typing ---
	void sendTyping(PeerId peerId, bool typing) override;

	// --- File Transfer ---
	void uploadFile(const FileRequest &request, GatewayCallback<FileResult> done, ErrorCallback fail) override;
	void downloadFile(const FileRequest &request, GatewayCallback<FileResult> done, ErrorCallback fail) override;

	// --- Updates Stream ---
	void setUpdateHandler(UpdateHandler handler) override;

	// Typing/presence push from the bridge ("typing" update kind, plus the
	// synthesised typing edge from stream.start/message.new). Concrete to
	// WsGateway so MockGateway needn't stub it. Drives the real send-action
	// indicator instead of rendering an empty bubble.
	using TypingHandler = std::function<void(PeerId peerId, bool typing)>;
	void setTypingHandler(TypingHandler handler);

	// New-session push from the bridge ("dialog.new" update kind): a brand-new
	// Hermes session appeared while connected. Carries the new peer so the
	// seeder can inject a chat-list row live, without a reconnect.
	using NewDialogHandler = std::function<void(const GatewayPeer &peer)>;
	void setNewDialogHandler(NewDialogHandler handler);

	// --- Search ---
	void searchMessages(PeerId peerId, const QString &query, int limit, GatewayCallback<std::vector<GatewayMessage>> done, ErrorCallback fail) override;

	// --- Fixture access (for the seeder) ---
	[[nodiscard]] const std::vector<GatewayDialog> &dialogs() const { return _dialogs; }
	[[nodiscard]] const std::unordered_map<PeerId, GatewayPeer> &peers() const { return _peers; }
	[[nodiscard]] const std::unordered_map<PeerId, std::vector<GatewayMessage>> &history() const { return _history; }

private:
	int nextRequestId();
	void onTextMessage(const QString &raw);
	void onConnected();
	void handleResponse(int id, const QJsonObject &obj);
	void handleUpdate(const QJsonObject &obj);
	void send(const QJsonObject &frame);

	WsClient *_ws = nullptr;
	QString _url;
	QString _token;
	QString _user;
	AuthState _authState = AuthState::LoggedOut;
	UpdateHandler _updateHandler;
	TypingHandler _typingHandler;
	NewDialogHandler _newDialogHandler;
	int _requestId = 0;

	bool _authed = false;
	bool _gotSessions = false;
	bool _rosterOnly = false;
	bool _gotRoster = false;
	int _pendingHistories = 0;

	std::vector<KnownUser> _roster;

	std::vector<GatewayDialog> _dialogs;
	std::unordered_map<PeerId, GatewayPeer> _peers;
	std::unordered_map<PeerId, std::vector<GatewayMessage>> _history;

	std::map<int, GatewayCallback<GatewayMessage>> _sendCallbacks;
	std::map<int, std::pair<PeerId, GatewayCallback<std::vector<GatewayMessage>>>> _historyCallbacks;
};

} // namespace OpenClaw
