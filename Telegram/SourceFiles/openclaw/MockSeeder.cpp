/*
This file is part of OCDesktop,
a native OpenClaw desktop client forked from Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/binary64/ocdesktop/blob/main/LICENSE
*/
#include "openclaw/MockSeeder.h"

#include "openclaw/MockGateway.h"
#include "openclaw/WsGateway.h"
#include "openclaw/ConnectConfig.h"
#include "openclaw/ConnectBox.h"
#include "openclaw/UserPickerBox.h"
#include "core/application.h"
#include "window/window_controller.h"
#include "ui/layers/generic_box.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "base/weak_ptr.h"
#include "base/unixtime.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "data/data_peer_id.h"
#include "data/data_messages.h"
#include "data/data_history_messages.h"
#include "history/history.h"
#include "history/history_item.h"
#include "ui/text/text_entity.h"
#include "api/api_text_entities.h"
#include "apiwrap.h"
#include "logs.h"

#include <QProcessEnvironment>
#include <QFile>
#include <memory>

namespace OpenClaw {
namespace {

std::unique_ptr<WsGateway> LiveGateway;

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
	// Hermes bridge peers are rendered as normal conversations, never bots.
	// Telegram shows a big "Start" button (instead of the message composer)
	// whenever a bot chat has empty history -- which is wrong for a bridge,
	// where every peer should be directly typeable. So we deliberately do NOT
	// set f_bot here, even if the gateway marks the peer as a bot.
	const auto botInfoVersion = MTPint();
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
		botInfoVersion, // bot_info_version
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
	const auto date = (msg.date > 0) ? msg.date : TimeId(base::unixtime::now());
	auto parsed = TextUtilities::ParseEntities(
		msg.text,
		TextParseLinks | TextParseMarkdown);
	const auto entities = Api::EntitiesToMTP(
		nullptr,
		parsed.entities,
		Api::ConvertOption::SkipLocal);
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
		MTP_int(date),
		MTP_string(parsed.text),
		MTP_messageMediaEmpty(),
		MTPReplyMarkup(),
		entities,
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

bool SeedingEnabled() {
	return true;
}

GatewayInterface *ActiveGateway() {
	return LiveGateway.get();
}

std::optional<MTPmessages_Messages> OfflineHistory(
		not_null<History*> history) {
	if (!LiveGateway) {
		return std::nullopt;
	}
	const auto peer = history->peer;
	if (!peer->asUser()) {
		return std::nullopt;
	}
	const auto gatewayPeerId = PeerId(peerToUser(peer->id).bare);

	std::vector<GatewayMessage> cached;
	LiveGateway->requestHistory(
		gatewayPeerId,
		MsgId(0),
		200,
		[&](std::vector<GatewayMessage> msgs) { cached = std::move(msgs); },
		[](const QString &) {});

	auto messages = QVector<MTPMessage>();
	messages.reserve(int(cached.size()));
	for (const auto &gm : cached) {
		if (!IsServerMsgId(::MsgId(gm.id))) {
			continue;
		}
		messages.push_back(MakeMessage(gm));
	}

	return MTP_messages_messages(
		MTP_vector<MTPMessage>(messages),
		MTP_vector<MTPForumTopic>(),
		MTP_vector<MTPChat>(),
		MTP_vector<MTPUser>());
}

template <typename Gateway>
bool SeedFromGateway(not_null<Main::Account*> account, Gateway &gateway) {
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

	account->setOfflineSession(true);
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

	LOG(("OpenClaw seeder: injected %1 users, %2 dialogs, %3 messages."
		).arg(users.size()).arg(dialogs.size()).arg(messages.size()));

	return true;
}

// Apply a gateway message to an EXISTING session, idempotently. Used by both
// the live update stream (stream.start / stream.delta / message.new) and the
// relaunch refresh. If a bubble with this id already exists we edit its text
// in place (so streaming deltas grow a single bubble, and re-delivered finals
// don't duplicate); otherwise we add a new bubble. This replaces the old
// "addNewMessage on every frame" handler, which crashed on the second frame
// because tdesktop's data layer rejects a duplicate message id.
void ApplyGatewayMessage(
		not_null<Main::Session*> session,
		const GatewayMessage &msg) {
	auto &data = session->data();
	const auto peerId = peerFromUser(UserId(msg.peerId));
	if (const auto existing = data.message(peerId, MsgId(msg.id))) {
		existing->setText(TextUtilities::ParseEntities(
			msg.text,
			TextParseLinks | TextParseMarkdown));
		data.requestItemTextRefresh(existing);
		data.notifyItemDataChange(existing);
		return;
	}
	data.addNewMessage(
		MakeMessage(msg),
		MessageFlags(),
		NewMessageType::Unread);
}

void WireUpdateHandler(not_null<Main::Account*> account) {
	if (!LiveGateway) {
		return;
	}
	const auto weakSession = base::make_weak(&account->session());
	LiveGateway->setUpdateHandler([weakSession](const GatewayMessage &msg) {
		crl::on_main([weakSession, msg] {
			const auto session = weakSession.get();
			if (!session) {
				return;
			}
			if (msg.fromId == kSelfBareId) {
				return;
			}
			ApplyGatewayMessage(session, msg);
		});
	});
}

// Reattach the live gateway to an ALREADY-EXISTING (persisted) session on
// relaunch: tdesktop seeds the session only on first run, so without this the
// gateway is never recreated and outgoing sends route nowhere. We rebuild the
// gateway, mark the session offline again (the flag is runtime-only, never
// persisted), refresh dialogs/messages from the bridge so stale cached history
// is corrected, and rewire the update handler.
static bool ReattachWithConfig(
		not_null<Main::Account*> account,
		const ConnectConfig &config) {
	if (!account->sessionExists() || config.url.isEmpty()) {
		return false;
	}
	LiveGateway = std::make_unique<WsGateway>(config.url, config.token, config.user);
	if (!LiveGateway->bootstrapBlocking()) {
		LOG(("OpenClaw reattach: WS bootstrap failed for %1").arg(config.url));
		LiveGateway = nullptr;
		return false;
	}
	account->setOfflineSession(true);

	auto &session = account->session();
	auto &data = session.data();

	auto users = QVector<MTPUser>();
	for (const auto &[id, peer] : LiveGateway->peers()) {
		if (id == PeerId(kSelfBareId)) {
			continue;
		}
		users.push_back(MakeUser(peer, false));
	}
	if (!users.isEmpty()) {
		data.processUsers(MTP_vector<MTPUser>(users));
	}

	auto messages = QVector<MTPMessage>();
	for (const auto &[peerId, msgs] : LiveGateway->history()) {
		for (const auto &msg : msgs) {
			messages.push_back(MakeMessage(msg));
		}
	}
	auto dialogs = QVector<MTPDialog>();
	for (const auto &dialog : LiveGateway->dialogs()) {
		dialogs.push_back(MakeDialog(dialog));
	}
	data.applyDialogs(nullptr, messages, dialogs);

	WireUpdateHandler(account);
	LOG(("OpenClaw reattach: refreshed %1 users, %2 dialogs, %3 messages."
		).arg(users.size()).arg(dialogs.size()).arg(messages.size()));
	return true;
}

// Core: given a resolved config, create the live session by bootstrapping the
// WS gateway. Returns true only when bootstrap + seed both succeed. On any
// failure the LiveGateway is torn down and false is returned so the caller can
// fall back to the ConnectBox.
static bool SeedWithConfig(
		not_null<Main::Account*> account,
		const ConnectConfig &config) {
	if (account->sessionExists()) {
		return false;
	}
	if (config.url.isEmpty()) {
		return false;
	}

	LiveGateway = std::make_unique<WsGateway>(config.url, config.token, config.user);
	if (!LiveGateway->bootstrapBlocking()) {
		LOG(("OpenClaw seeder: WS bootstrap failed for %1").arg(config.url));
		LiveGateway = nullptr;
		return false;
	}
	if (!SeedFromGateway(account, *LiveGateway)) {
		LiveGateway = nullptr;
		return false;
	}

	WireUpdateHandler(account);
	return true;
}

bool SeedMockSession(not_null<Main::Account*> account) {
	if (account->sessionExists()) {
		return false;
	}

	const auto config = ResolveConnectConfig();
	if (config.valid()) {
		if (SeedWithConfig(account, config)) {
			return true;
		}
	}
	return false;
}

static void ShowAccountWhenSeeded(not_null<Main::Account*> account) {
	if (!account->sessionExists()) {
		return;
	}
	crl::on_main([account] {
		auto &app = Core::App();
		if (const auto window = app.activePrimaryWindow()) {
			LOG(("OpenClaw: showing seeded account."));
			window->showAccount(account);
		}
		MaybeAutoStartBot(account);
	});
}

static void SeedAndShow(
		not_null<Main::Account*> account,
		const ConnectConfig &config) {
	if (SeedWithConfig(account, config)) {
		SaveConnectConfig(config);
		LOG(("OpenClaw connect: seeded for user %1.").arg(
			config.user.isEmpty() ? QString("(none)") : config.user));
		ShowAccountWhenSeeded(account);
	} else {
		LOG(("OpenClaw connect: seed failed for user %1.").arg(config.user));
	}
}

void StartConnectFlow(
		not_null<Main::Account*> account,
		Window::Controller *window) {
	const auto resolved = ResolveConnectConfig();

	// Relaunch path: the session is already persisted, so tdesktop won't
	// re-seed. Reattach the live gateway (and refresh stale cached history)
	// so outgoing sends route to the bridge and chats show current content.
	if (account->sessionExists()) {
		if (resolved.valid() && ReattachWithConfig(account, resolved)) {
			LOG(("OpenClaw connect: reattached live gateway on relaunch."));
		} else {
			LOG(("OpenClaw connect: session exists but reattach failed; "
				"sends will not reach the bridge until next clean seed."));
		}
		return;
	}

	// No bridge URL/token yet → the classic connect screen handles that first.
	if (!resolved.valid()) {
		if (!window) {
			LOG(("OpenClaw connect: no window to show ConnectBox; staying on intro."));
			return;
		}
		const auto initial = LoadConnectConfig().valid()
			? LoadConnectConfig()
			: ResolveConnectConfig();
		const auto attempt = [account](
				ConnectConfig config,
				Fn<void(ConnectResult)> report) {
			const auto ok = SeedWithConfig(account, config);
			if (ok) {
				SaveConnectConfig(config);
				ShowAccountWhenSeeded(account);
				report(ConnectResult::Success);
			} else {
				report(ConnectResult::Failed);
			}
		};
		crl::on_main([initial, attempt] {
			if (const auto window = Core::App().activePrimaryWindow()) {
				window->show(Box(ConnectBox, initial, attempt));
			}
		});
		return;
	}

	// Bridge is configured. If a user was already chosen, seed straight away;
	// otherwise show the household picker so each person sees only their chats.
	if (resolved.hasUser()) {
		SeedAndShow(account, resolved);
		return;
	}

	crl::on_main([account, resolved] {
		const auto window = Core::App().activePrimaryWindow();
		if (!window) {
			LOG(("OpenClaw connect: no window for user picker; staying on intro."));
			return;
		}
		const auto pick = [account, resolved](QString userId) {
			auto config = resolved;
			config.user = userId;
			SeedAndShow(account, config);
		};
		LOG(("OpenClaw connect: showing user picker."));
		window->show(Box(UserPickerBox, QString(), pick));
	});
}

void SwitchUser(not_null<Main::Account*> account) {
	auto config = ResolveConnectConfig();
	config.user = QString();
	SaveConnectConfig(config);
	LiveGateway = nullptr;
	account->forcedLogOut();
	crl::on_main([account] {
		StartConnectFlow(account, Core::App().activePrimaryWindow());
	});
}

void MaybeAutoStartBot(not_null<Main::Account*> account) {
	const auto env = QProcessEnvironment::systemEnvironment();
	const auto flag = env.value("OCDESKTOP_AUTOSTART_BOT");
	if (flag.isEmpty() || flag == "0") {
		return;
	}
	if (!account->sessionExists()) {
		LOG(("OpenClaw autostart: no session, skipping."));
		return;
	}
	const auto botBareId = flag.toULongLong() > 1
		? flag.toULongLong()
		: uint64(101);
	auto &session = account->session();
	auto bot = [&]() -> UserData* {
		const auto peer = session.data().peerLoaded(
			peerFromUser(UserId(botBareId)));
		if (const auto user = peer ? peer->asUser() : nullptr) {
			if (user->isBot()) {
				return user;
			}
		}
		UserData *firstBot = nullptr;
		session.data().enumerateUsers([&](not_null<UserData*> user) {
			if (!firstBot && user->isBot()) {
				firstBot = user;
			}
		});
		return firstBot;
	}();
	if (!bot) {
		LOG(("OpenClaw autostart: no bot peer found (looked for %1, "
			"then scanned all users).").arg(botBareId));
		return;
	}
	LOG(("OpenClaw autostart: invoking sendBotStart on %1 (id=%2, isBot=%3)."
		).arg(bot->name()
		).arg(peerToUser(bot->id).bare
		).arg(bot->isBot() ? "true" : "false"));
	session.api().sendBotStart(nullptr, bot);
	LOG(("OpenClaw autostart: sendBotStart returned without crash."));
}

} // namespace OpenClaw
