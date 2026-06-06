/*
This file is part of OCDesktop,
a native OpenClaw desktop client forked from Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/binary64/ocdesktop/blob/main/LICENSE
*/
#include "openclaw/ConnectBox.h"

#include "ui/layers/generic_box.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "base/weak_ptr.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

#include <rpl/rpl.h>

namespace OpenClaw {

void ConnectBox(
		not_null<Ui::GenericBox*> box,
		ConnectConfig initial,
		Fn<void(ConnectConfig, Fn<void(ConnectResult)>)> attempt) {
	box->setTitle(rpl::single(QString("Connect to Hermes")));

	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::single(QString(
			"Enter the OpenClaw bridge address and access token. "
			"These are saved, so you only do this once.")),
		st::boxLabel));

	box->addSkip(st::boxLittleSkip);

	const auto url = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		Ui::InputField::Mode::SingleLine,
		rpl::single(QString("ws://host:port/ocdesktop")),
		initial.url));

	box->addSkip(st::boxLittleSkip);

	const auto token = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		Ui::InputField::Mode::SingleLine,
		rpl::single(QString("Access token")),
		initial.token));

	const auto error = box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::single(QString()),
		st::boxLabel));
	error->setTextColorOverride(st::boxTextFgError->c);
	error->hide();

	const auto attempting = box->lifetime().make_state<bool>(false);

	const auto submit = [=] {
		if (*attempting) {
			return;
		}
		auto config = ConnectConfig();
		config.url = url->getLastText().trimmed();
		config.token = token->getLastText().trimmed();
		if (config.url.isEmpty()) {
			error->setText("Please enter a bridge address.");
			error->show();
			url->showError();
			return;
		}
		*attempting = true;
		error->setText("Connecting\xE2\x80\xA6");
		error->show();

		attempt(config, crl::guard(box, [=](ConnectResult result) {
			*attempting = false;
			if (result == ConnectResult::Success) {
				box->closeBox();
			} else {
				error->setText(
					"Couldn't connect. Check the address and token, "
					"and that the bridge is reachable.");
				error->show();
			}
		}));
	};

	url->submits() | rpl::on_next([=] { token->setFocus(); }, box->lifetime());
	token->submits() | rpl::on_next([=] { submit(); }, box->lifetime());

	box->addButton(rpl::single(QString("Connect")), submit);
	box->addButton(rpl::single(QString("Quit")), [=] {
		box->closeBox();
	});

	box->setFocusCallback([=] {
		if (initial.url.isEmpty()) {
			url->setFocusFast();
		} else {
			token->setFocusFast();
		}
	});
}

} // namespace OpenClaw
