/*
This file is part of OCDesktop,
a native OpenClaw desktop client forked from Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/binary64/ocdesktop/blob/main/LICENSE
*/
#pragma once

#include <QString>
#include <QByteArray>
#include <functional>
#include <optional>
#include <cstdint>
#include <vector>

namespace OpenClaw {

// Forward declarations for types shared with the UI layer.
// These mirror tdesktop's data model types so the UI doesn't need
// to change — only the gateway implementation behind this interface.
using PeerId = uint64;
using MsgId = int64;
using TimeId = int32;

// Minimal message representation for the gateway boundary.
struct GatewayMessage {
	MsgId id = 0;
	PeerId peerId = 0;
	PeerId fromId = 0;
	QString text;
	TimeId date = 0;
	MsgId replyToMsgId = 0;
	// Media attachments handled via separate file transfer interface.
};

// Chat/dialog metadata at the gateway boundary.
struct GatewayDialog {
	PeerId peerId = 0;
	QString title;
	int unreadCount = 0;
	MsgId topMessageId = 0;
	bool isPinned = false;
	bool isArchived = false;
};

// Peer info at the gateway boundary.
struct GatewayPeer {
	PeerId id = 0;
	QString name;
	QString username;
	QString phone;
	QString about;
	bool isBot = false;
	bool isGroup = false;
	bool isChannel = false;
	bool isOnline = false;
	TimeId lastSeen = 0;
};

// File transfer request/result.
struct FileRequest {
	QString fileId;
	QString localPath;
	int64_t size = 0;
	QString mimeType;
};

struct FileResult {
	QString localPath;
	QByteArray data; // For small files / thumbnails.
	bool success = false;
	QString error;
};

// Auth state.
enum class AuthState {
	LoggedOut,
	WaitingForCode,
	WaitingForPassword,
	Ready,
};

// Callback types.
template <typename T>
using GatewayCallback = std::function<void(T)>;
using ErrorCallback = std::function<void(const QString &error)>;
using DoneCallback = std::function<void()>;

// ============================================================
// GatewayInterface — the abstraction layer between UI and
// whatever messaging backend is active (MTProto or OpenClaw).
//
// Phase 1: MtprotoGateway implements this by delegating to
//          the existing ApiWrap / MTP::Sender code.
// Phase 2: OpenClawGateway implements this via WebSocket/REST
//          to the OpenClaw gateway daemon.
// ============================================================
class GatewayInterface {
public:
	virtual ~GatewayInterface() = default;

	// --- Auth ---
	[[nodiscard]] virtual AuthState authState() const = 0;
	virtual void logout(DoneCallback done) = 0;

	// --- Dialogs / Chat List ---
	virtual void requestDialogs(
		bool archived,
		GatewayCallback<std::vector<GatewayDialog>> done,
		ErrorCallback fail) = 0;
	virtual void requestPinnedDialogs(
		bool archived,
		GatewayCallback<std::vector<GatewayDialog>> done,
		ErrorCallback fail) = 0;
	virtual void savePinnedOrder(
		const std::vector<PeerId> &order,
		bool archived,
		DoneCallback done,
		ErrorCallback fail) = 0;

	// --- Messages ---
	virtual void sendMessage(
		PeerId peerId,
		const QString &text,
		MsgId replyTo,
		GatewayCallback<GatewayMessage> done,
		ErrorCallback fail) = 0;
	virtual void requestHistory(
		PeerId peerId,
		MsgId offsetId,
		int limit,
		GatewayCallback<std::vector<GatewayMessage>> done,
		ErrorCallback fail) = 0;
	virtual void requestMessageData(
		PeerId peerId,
		MsgId msgId,
		GatewayCallback<GatewayMessage> done,
		ErrorCallback fail) = 0;
	virtual void deleteMessages(
		PeerId peerId,
		const std::vector<MsgId> &ids,
		bool revoke,
		DoneCallback done,
		ErrorCallback fail) = 0;
	virtual void markContentsRead(
		PeerId peerId,
		const std::vector<MsgId> &ids,
		DoneCallback done) = 0;

	// --- Peer Info ---
	virtual void requestFullPeer(
		PeerId peerId,
		GatewayCallback<GatewayPeer> done,
		ErrorCallback fail) = 0;
	virtual void requestContacts(
		GatewayCallback<std::vector<GatewayPeer>> done,
		ErrorCallback fail) = 0;

	// --- Presence & Typing ---
	virtual void sendTyping(PeerId peerId, bool typing) = 0;

	// --- File Transfer ---
	virtual void uploadFile(
		const FileRequest &request,
		GatewayCallback<FileResult> done,
		ErrorCallback fail) = 0;
	virtual void downloadFile(
		const FileRequest &request,
		GatewayCallback<FileResult> done,
		ErrorCallback fail) = 0;

	// --- Updates Stream ---
	// The gateway pushes real-time updates (new messages, presence,
	// typing, etc.) via this callback. The UI registers once at startup.
	using UpdateHandler = std::function<void(const GatewayMessage &msg)>;
	virtual void setUpdateHandler(UpdateHandler handler) = 0;

	// --- Search ---
	virtual void searchMessages(
		PeerId peerId, // 0 = global search
		const QString &query,
		int limit,
		GatewayCallback<std::vector<GatewayMessage>> done,
		ErrorCallback fail) = 0;
};

} // namespace OpenClaw
