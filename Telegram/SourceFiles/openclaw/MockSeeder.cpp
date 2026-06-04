/*
This file is part of OCDesktop,
a native OpenClaw desktop client forked from Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/binary64/ocdesktop/blob/main/LICENSE
*/
#include "openclaw/MockSeeder.h"

#include "openclaw/MockGateway.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_peer_id.h"
#include "history/history.h"
#include "apiwrap.h"
#include "logs.h"

#include <QProcessEnvironment>

namespace OpenClaw {
namespace {

constexpr auto kSelfBareId = uint64(1);

[[nodiscard]] MTPPeer PeerToMtp(PeerId bareId) {
	return MTP_peerUser(MTP_long(bareId));
}

[[nodiscard]] MTPUser MakeUser(const GatewayPeer &peer, bool isSelf) {
	using Flag = MTPDuser::Flag;
	auto flags = Flag::f_access_hash
		| Flag::f_first_name
		| Flag::f_username;
	if (isSelf) {
		flags |= Flag::f_self;
	} else {
		flags |= Flag::f_contact;
	}
	if (peer.isBot) {
		flags |= Flag::f_bot;
	}
	return MTP_user(
		MTP_flags(flags),
		MTP_long(peer.id),
		MTP_long(0), // access_hash
		MTP_string(peer.name),
		MTPstring(), // last_name
		MTP_string(peer.username),
		MTPstring(), // phone
		MTPUserProfilePhoto(),
		MTPUserStatus(),
		MTPint(), // bot_info_version
		MTPVector<MTPRestrictionReason>(),
		MTPstring(), // bot_inline_placeholder
		MTPstring(), // lang_code
		MTPEmojiStatus(),
		MTPVector<MTPUsername>(),
		MTPRecentStory(),
		MTPPeerColor(),
		MTPPeerColor(),
		MTPint(), // bot_active_users
		MTPlong(), // bot_verification_icon
		MTPlong()); // send_paid_messages_stars
}

[[nodiscard]] MTPMessage MakeMessage(const GatewayMessage &msg) {
	using Flag = MTPDmessage::Flag;
	auto flags = Flag::f_from_id | Flag();
	if (msg.fromId == kSelfBareId) {
		flags |= Flag::f_out;
	}
	return MTP_message(
		MTP_flags(flags),
		MTP_int(int(msg.id)),
		PeerToMtp(msg.fromId),
		MTPint(), // from_boosts_applied
		MTPstring(), // from_rank
		PeerToMtp(msg.peerId),
		MTPPeer(), // saved_peer_id
		MTPMessageFwdHeader(),
		MTPlong(), // via_bot_id
		MTPlong(), // via_business_bot_id
		MTPPeer(), // guestchat_via_from
		MTPMessageReplyHeader(),
		MTP_int(msg.date),
		MTP_string(msg.text),
		MTP_messageMediaEmpty(),
		MTPReplyMarkup(),
		MTPVector<MTPMessageEntity>(),
		MTPint(), // views
		MTPint(), // forwards
		MTPMessageReplies(),
		MTPint(), // edit_date
		MTPstring(), // post_author
		MTPlong(), // grouped_id
		MTPMessageReactions(),
		MTPVector<MTPRestrictionReason>(),
		MTPint(), // ttl_period
		MTPint(), // quick_reply_shortcut_id
		MTPlong(), // effect
		MTPFactCheck(),
		MTPint(), // report_delivery_until_date
		MTPlong(), // paid_message_stars
		MTPSuggestedPost(),
		MTPint(), // schedule_repeat_period
		MTPstring()); // summary_from_language
}

[[nodiscard]] MTPDialog MakeDialog(const GatewayDialog &dialog) {
	using Flag = MTPDdialog::Flag;
	auto flags = Flag() | Flag();
	if (dialog.isPinned) {
		flags |= Flag::f_pinned;
	}
	return MTP_dialog(
		MTP_flags(flags),
		PeerToMtp(dialog.peerId),
		MTP_int(int(dialog.topMessageId)),
		MTPint(), // read_inbox_max_id
		MTPint(), // read_outbox_max_id
		MTP_int(dialog.unreadCount),
		MTPint(), // unread_mentions_count
		MTPint(), // unread_reactions_count
		MTPint(), // unread_poll_votes_count
		MTP_peerNotifySettings(
			MTP_flags(MTPDpeerNotifySettings::Flag()),
			MTPBool(), // show_previews
			MTPBool(), // silent
			MTPint(), // mute_until
			MTPNotificationSound(), // ios_sound
			MTPNotificationSound(), // android_sound
			MTPNotificationSound(), // other_sound
			MTPBool(), // stories_muted
			MTPBool(), // stories_hide_sender
			MTPNotificationSound(), // stories_ios_sound
			MTPNotificationSound(), // stories_android_sound
			MTPNotificationSound()), // stories_other_sound
		MTPint(), // pts
		MTPDraftMessage(),
		MTPint(), // folder_id
		MTPint()); // ttl_period
}

} // namespace

bool MockModeEnabled() {
	const auto env = QProcessEnvironment::systemEnvironment();
	const auto value = env.value("OCDESKTOP_MOCK");
	return (value == "1")
		|| (value.compare("true", Qt::CaseInsensitive) == 0)
		|| (value.compare("yes", Qt::CaseInsensitive) == 0);
}

bool SeedMockSession(not_null<Main::Account*> account) {
	if (!MockModeEnabled()) {
		return false;
	}
	if (account->sessionExists()) {
		return false;
	}

	MockGateway gateway;

	auto settings = std::make_unique<Main::SessionSettings>();
	const auto selfPeer = [&]() -> GatewayPeer {
		const auto &peers = gateway.peers();
		if (const auto it = peers.find(PeerId(kSelfBareId));
				it != peers.end()) {
			return it->second;
		}
		GatewayPeer self;
		self.id = kSelfBareId;
		self.name = "You";
		self.username = "you";
		return self;
	}();

	account->createSession(MakeUser(selfPeer, true), std::move(settings));

	if (!account->sessionExists()) {
		return false;
	}
	auto &session = account->session();
	auto &data = session.data();

	auto users = QVector<MTPUser>();
	users.reserve(int(gateway.peers().size()) + 1);
	for (const auto &[id, peer] : gateway.peers()) {
		if (id == PeerId(kSelfBareId)) {
			continue;
		}
		users.push_back(MakeUser(peer, false));
	}
	if (!users.isEmpty()) {
		data.processUsers(MTP_vector<MTPUser>(users));
	}

	auto messages = QVector<MTPMessage>();
	for (const auto &[peerId, msgs] : gateway.history()) {
		for (const auto &msg : msgs) {
			messages.push_back(MakeMessage(msg));
		}
	}

	auto dialogs = QVector<MTPDialog>();
	dialogs.reserve(int(gateway.dialogs().size()));
	for (const auto &dialog : gateway.dialogs()) {
		dialogs.push_back(MakeDialog(dialog));
	}

	data.applyDialogs(nullptr, messages, dialogs);

	LOG(("OpenClaw MockSeeder: injected %1 users, %2 dialogs, %3 messages."
		).arg(users.size()).arg(dialogs.size()).arg(messages.size()));

	return true;
}

} // namespace OpenClaw
