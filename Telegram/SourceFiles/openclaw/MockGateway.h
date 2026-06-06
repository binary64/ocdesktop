/*
This file is part of OCDesktop,
a native OpenClaw desktop client forked from Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/binary64/ocdesktop/blob/main/LICENSE
*/
#pragma once

#include "openclaw/GatewayInterface.h"

#include <unordered_map>

namespace OpenClaw {

// ============================================================
// MockGateway — Phase 1 implementation of GatewayInterface.
//
// Fabricates dialogs, history, peers and a synthetic update
// stream entirely in memory. NO network, NO Telegram login.
//
// Purpose: prove the UI ↔ gateway seam is complete. If the
// client is fully navigable against this mock, then HermesGateway
// (Phase 2) only has to satisfy the same contract.
//
// Each mock "dialog" stands in for what will become a Hermes
// session; each mock message for a turn in that session.
// ============================================================
class MockGateway final : public GatewayInterface {
public:
	MockGateway();
	~MockGateway() override = default;

	// --- Auth ---
	[[nodiscard]] AuthState authState() const override;
	void logout(DoneCallback done) override;

	// --- Dialogs / Chat List ---
	void requestDialogs(
		bool archived,
		GatewayCallback<std::vector<GatewayDialog>> done,
		ErrorCallback fail) override;
	void requestPinnedDialogs(
		bool archived,
		GatewayCallback<std::vector<GatewayDialog>> done,
		ErrorCallback fail) override;
	void savePinnedOrder(
		const std::vector<PeerId> &order,
		bool archived,
		DoneCallback done,
		ErrorCallback fail) override;

	// --- Messages ---
	void sendMessage(
		PeerId peerId,
		const QString &text,
		MsgId replyTo,
		GatewayCallback<GatewayMessage> done,
		ErrorCallback fail) override;
	void requestHistory(
		PeerId peerId,
		MsgId offsetId,
		int limit,
		GatewayCallback<std::vector<GatewayMessage>> done,
		ErrorCallback fail) override;
	void requestMessageData(
		PeerId peerId,
		MsgId msgId,
		GatewayCallback<GatewayMessage> done,
		ErrorCallback fail) override;
	void deleteMessages(
		PeerId peerId,
		const std::vector<MsgId> &ids,
		bool revoke,
		DoneCallback done,
		ErrorCallback fail) override;
	void markContentsRead(
		PeerId peerId,
		const std::vector<MsgId> &ids,
		DoneCallback done) override;

	// --- Peer Info ---
	void requestFullPeer(
		PeerId peerId,
		GatewayCallback<GatewayPeer> done,
		ErrorCallback fail) override;
	void requestContacts(
		GatewayCallback<std::vector<GatewayPeer>> done,
		ErrorCallback fail) override;

	// --- Presence & Typing ---
	void sendTyping(PeerId peerId, bool typing) override;

	// --- File Transfer ---
	void uploadFile(
		const FileRequest &request,
		GatewayCallback<FileResult> done,
		ErrorCallback fail) override;
	void downloadFile(
		const FileRequest &request,
		GatewayCallback<FileResult> done,
		ErrorCallback fail) override;

	// --- Updates Stream ---
	void setUpdateHandler(UpdateHandler handler) override;

	// --- Search ---
	void searchMessages(
		PeerId peerId,
		const QString &query,
		int limit,
		GatewayCallback<std::vector<GatewayMessage>> done,
		ErrorCallback fail) override;

	// --- Fixture access (for MockSeeder) ---
	[[nodiscard]] const std::vector<GatewayDialog> &dialogs() const {
		return _dialogs;
	}
	[[nodiscard]] const std::unordered_map<PeerId, GatewayPeer> &peers() const {
		return _peers;
	}
	[[nodiscard]] const std::unordered_map<PeerId, std::vector<GatewayMessage>> &history() const {
		return _history;
	}

private:
	void seedFixtures();

	AuthState _authState = AuthState::Ready;
	UpdateHandler _updateHandler;
	MsgId _nextMsgId = 1000;

	std::vector<GatewayDialog> _dialogs;
	std::unordered_map<PeerId, GatewayPeer> _peers;
	std::unordered_map<PeerId, std::vector<GatewayMessage>> _history;
};

} // namespace OpenClaw
