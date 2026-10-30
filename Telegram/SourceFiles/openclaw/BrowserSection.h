/*
This file is part of the OCDesktop fork of Telegram Desktop.
*/
#pragma once

#include "window/section_widget.h"
#include "window/section_memento.h"
#include "base/object_ptr.h"

#include <QtCore/QString>

namespace Ui {
class InputField;
class RpWidget;
class PlainShadow;
} // namespace Ui

namespace Webview {
class Window;
} // namespace Webview

namespace OpenClaw {

class BrowserMemento final : public Window::SectionMemento {
public:
	explicit BrowserMemento(uint64 peerId, const QString &url);

	object_ptr<Window::SectionWidget> createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) override;

	[[nodiscard]] uint64 peerId() const {
		return _peerId;
	}
	[[nodiscard]] QString url() const {
		return _url;
	}

private:
	uint64 _peerId = 0;
	QString _url;

};

class BrowserSection final : public Window::SectionWidget {
public:
	BrowserSection(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		uint64 peerId,
		const QString &url);
	~BrowserSection();

	bool showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) override;
	std::shared_ptr<Window::SectionMemento> createMemento() override;

	bool hasTopBarShadow() const override {
		return true;
	}

	void navigateTo(const QString &url);

	bool floatPlayerHandleWheelEvent(QEvent *e) override {
		return false;
	}
	QRect floatPlayerAvailableRect() override {
		return QRect(mapToGlobal(QPoint()), size());
	}

	// Server->client push rail entry: drive the open browser for a peer.
	// Returns true if a live BrowserSection for that peer handled it.
	static bool NavigatePeer(uint64 peerId, const QString &url);

	// Last URL the user/agent navigated to for a peer (for convo-open restore).
	[[nodiscard]] static QString LastUrlFor(uint64 peerId);
	static void RememberUrl(uint64 peerId, const QString &url);

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void doSetInnerFocus() override;

private:
	void init();
	void createWebview();
	void layoutPieces();
	void commitUrlFromBar();

	uint64 _peerId = 0;
	QString _pendingUrl;
	object_ptr<Ui::RpWidget> _container;
	object_ptr<Ui::InputField> _urlBar = { nullptr };
	object_ptr<Ui::PlainShadow> _barShadow = { nullptr };
	std::unique_ptr<Webview::Window> _webview;
	bool _webviewReady = false;

};

} // namespace OpenClaw
