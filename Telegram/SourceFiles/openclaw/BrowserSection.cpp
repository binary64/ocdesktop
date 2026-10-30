/*
This file is part of the OCDesktop fork of Telegram Desktop.
*/
#include "openclaw/BrowserSection.h"

#include "window/window_session_controller.h"
#include "window/section_widget.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/shadow.h"
#include "ui/rp_widget.h"
#include "ui/painter.h"
#include "webview/webview_embed.h"
#include "webview/webview_interface.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

#include <QtGui/QPainter>
#include <map>

namespace OpenClaw {
namespace {

constexpr auto kBarHeight = 40;
constexpr auto kDefaultUrl = "https://duckduckgo.com/";

std::map<uint64, BrowserSection*> &LiveSections() {
	static auto map = std::map<uint64, BrowserSection*>();
	return map;
}

std::map<uint64, QString> &LastUrls() {
	static auto map = std::map<uint64, QString>();
	return map;
}

[[nodiscard]] QString NormalizeUrl(const QString &raw) {
	auto url = raw.trimmed();
	if (url.isEmpty()) {
		return QString();
	}
	if (!url.contains(u"://"_q)) {
		const auto looksLikeHost = url.contains('.') && !url.contains(' ');
		if (looksLikeHost) {
			url = u"https://"_q + url;
		} else {
			url = u"https://duckduckgo.com/?q="_q
				+ QString::fromUtf8(QUrl::toPercentEncoding(url));
		}
	}
	return url;
}

} // namespace

BrowserMemento::BrowserMemento(uint64 peerId, const QString &url)
: _peerId(peerId)
, _url(url) {
}

object_ptr<Window::SectionWidget> BrowserMemento::createWidget(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Window::Column column,
		const QRect &geometry) {
	auto result = object_ptr<BrowserSection>(
		parent,
		controller,
		_peerId,
		_url);
	result->setGeometry(geometry);
	return result;
}

BrowserSection::BrowserSection(
	QWidget *parent,
	not_null<Window::SessionController*> controller,
	uint64 peerId,
	const QString &url)
: Window::SectionWidget(parent, controller, rpl::producer<PeerData*>(rpl::never<PeerData*>()))
, _peerId(peerId)
, _pendingUrl(url.isEmpty() ? LastUrlFor(peerId) : url)
, _container(this) {
	if (_pendingUrl.isEmpty()) {
		_pendingUrl = QString::fromUtf8(kDefaultUrl);
	}
	init();
}

BrowserSection::~BrowserSection() {
	const auto i = LiveSections().find(_peerId);
	if (i != end(LiveSections()) && i->second == this) {
		LiveSections().erase(i);
	}
}

void BrowserSection::init() {
	LiveSections()[_peerId] = this;

	_urlBar.create(
		this,
		st::defaultInputField,
		rpl::single(QString("Address")),
		_pendingUrl);
	_urlBar->show();
	_urlBar->submits(
	) | rpl::on_next([=](auto) {
		commitUrlFromBar();
	}, _urlBar->lifetime());

	_barShadow.create(this);
	_barShadow->show();

	_container->show();
	_container->setGeometry(QRect(
		0,
		kBarHeight,
		width(),
		std::max(0, height() - kBarHeight)));

	createWebview();
	layoutPieces();
}

void BrowserSection::createWebview() {
	_webview = std::make_unique<Webview::Window>(
		_container.data(),
		Webview::WindowConfig{
			.opaqueBg = st::windowBg->c,
			.safe = true,
		});
	const auto raw = _webview.get();
	const auto widget = raw->widget();
	if (!widget) {
		_webview = nullptr;
		_webviewReady = false;
		return;
	}
	_webviewReady = true;
	widget->show();
	widget->setGeometry(QRect(QPoint(), _container->size()));

	raw->setNavigationDoneHandler([=](bool success) {
	});

	QObject::connect(widget, &QObject::destroyed, [=] {
		_webviewReady = false;
	});

	const auto url = NormalizeUrl(_pendingUrl);
	if (!url.isEmpty()) {
		raw->navigate(url);
		RememberUrl(_peerId, url);
	}
}

void BrowserSection::layoutPieces() {
	_urlBar->resizeToWidth(width() - 2 * st::boxRowPadding.left());
	_urlBar->moveToLeft(
		st::boxRowPadding.left(),
		(kBarHeight - _urlBar->height()) / 2);
	_barShadow->setGeometry(0, kBarHeight - st::lineWidth, width(), st::lineWidth);
	_container->setGeometry(QRect(
		0,
		kBarHeight,
		width(),
		std::max(0, height() - kBarHeight)));
	if (_webview && _webview->widget()) {
		_webview->widget()->setGeometry(QRect(QPoint(), _container->size()));
	}
}

void BrowserSection::commitUrlFromBar() {
	const auto url = NormalizeUrl(_urlBar->getLastText());
	if (url.isEmpty()) {
		return;
	}
	navigateTo(url);
}

void BrowserSection::navigateTo(const QString &url) {
	const auto normalized = NormalizeUrl(url);
	if (normalized.isEmpty()) {
		return;
	}
	_pendingUrl = normalized;
	RememberUrl(_peerId, normalized);
	if (_urlBar && _urlBar->getLastText() != normalized) {
		_urlBar->setText(normalized);
	}
	if (_webview && _webviewReady) {
		_webview->navigate(normalized);
	}
}

bool BrowserSection::showInternal(
		not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	return false;
}

std::shared_ptr<Window::SectionMemento> BrowserSection::createMemento() {
	return std::make_shared<BrowserMemento>(_peerId, _pendingUrl);
}

void BrowserSection::resizeEvent(QResizeEvent *e) {
	Window::SectionWidget::resizeEvent(e);
	if (_urlBar) {
		layoutPieces();
	}
}

void BrowserSection::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.fillRect(e->rect(), st::windowBg);
}

void BrowserSection::doSetInnerFocus() {
	if (_urlBar) {
		_urlBar->setFocusFast();
	}
}

bool BrowserSection::NavigatePeer(uint64 peerId, const QString &url) {
	RememberUrl(peerId, NormalizeUrl(url));
	const auto i = LiveSections().find(peerId);
	if (i == end(LiveSections()) || !i->second) {
		return false;
	}
	i->second->navigateTo(url);
	return true;
}

QString BrowserSection::LastUrlFor(uint64 peerId) {
	const auto i = LastUrls().find(peerId);
	return (i != end(LastUrls())) ? i->second : QString();
}

void BrowserSection::RememberUrl(uint64 peerId, const QString &url) {
	if (!url.isEmpty()) {
		LastUrls()[peerId] = url;
	}
}

} // namespace OpenClaw
