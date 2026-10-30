/*
This file is part of OCDesktop,
a native OpenClaw desktop client forked from Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/binary64/ocdesktop/blob/main/LICENSE
*/
#include "openclaw/UserPickerBox.h"
#include "openclaw/ConnectConfig.h"

#include "ui/layers/generic_box.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

#include <rpl/rpl.h>
#include <crl/crl_on_main.h>

namespace OpenClaw {

void UserPickerBox(
		not_null<Ui::GenericBox*> box,
		std::vector<KnownUser> members,
		const QString &current,
		Fn<void(QString userId)> chosen) {
	box->setTitle(rpl::single(QString("Welcome to OCDesktop")));
	box->setWidth(st::boxWideWidth);

	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::single(QString(
			"Tap who you are — you'll see only your own chats with Hermes. "
			"You can switch later from the menu.")),
		st::boxLabel));

	box->addSkip(st::boxMediumSkip);

	const auto sidePadding = st::boxRowPadding.left()
		+ st::boxRowPadding.right();
	for (const auto &user : members) {
		const auto id = user.id;
		auto title = user.label;
		if (id == current) {
			title += QString(" (current)");
		}
		const auto button = box->addRow(
			object_ptr<Ui::RoundButton>(
				box,
				rpl::single(title),
				st::changePhoneButton),
			style::margins(
				st::boxRowPadding.left(),
				st::boxLittleSkip,
				st::boxRowPadding.right(),
				st::boxLittleSkip));
		box->widthValue(
		) | rpl::on_next([=](int width) {
			button->setFullWidth(width - sidePadding);
		}, button->lifetime());
		button->setClickedCallback([=] {
			box->closeBox();
			crl::on_main([=] {
				chosen(id);
			});
		});
	}

	box->addSkip(st::boxLittleSkip);
}

} // namespace OpenClaw
