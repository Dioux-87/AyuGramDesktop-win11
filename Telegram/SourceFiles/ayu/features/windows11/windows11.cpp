// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/features/windows11/windows11.h"

#include "ayu/ayu_settings.h"
#include "ayu/features/windows11/platform/platform_windows11.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "ui/style/style_core_palette.h"
#include "window/themes/window_theme.h"
#include "window/window_controller.h"

#include <QtCore/QByteArray>
#include <QtCore/QEvent>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtGui/QColor>
#include <QtGui/QPainter>
#include <QtWidgets/QWidget>

#include <array>
#include <optional>
#include <vector>

namespace AyuFeatures::Windows11 {
namespace {

rpl::lifetime Lifetime;

InterfaceTheme Applied = InterfaceTheme::System;

// Whether the window the user is looking at is active. This no longer decides
// the surface: it only tells the backdrop code whether DWM should draw the
// material in its active or inactive tint. Windows itself flattens the
// material of an inactive window; there is nothing to switch, and switching
// it is what used to destroy the native window.
bool ActiveWindow = true;

// The composition is fixed for the lifetime of the process. See the long
// comment in TranslucentWindow().
Platform::Surface SessionSurface = Platform::Surface::Opaque;
bool SessionSurfaceDecided = false;
bool SessionBackgroundChecked = false;
bool SessionDefaultBackground = true;

// Defined below; used by the activation filter, which must only repaint.
void ForceRepaint();

// The colors that make the "backgrounds" of the whole interface. In the
// default theme they all point to the window color, so making them translucent
// is what turns the window, the chat list, the top bar, the boxes, the menus
// and the settings into panels over the Mica material.
//
// Only the alpha channel changes. The RGB is taken from the color of the
// current theme, so a custom theme keeps its own shades and only loses its
// opacity.
constexpr auto kTranslucentColors = std::array{
	// The main window.
	"windowBg",
	"windowBgOver",
	"windowBgRipple",
	// Chat list.
	"dialogsBg",
	"dialogsBgOver",
	// Chat view.
	"topBarBg",
	// Boxes (dialogs) and the layer behind them.
	"boxBg",
	"boxSearchBg",
	"boxDividerBg",
	// Menus and tooltips.
	"menuBg",
	"menuBgOver",
	"menuBgRipple",
	"tooltipBg",
	// Emoji and stickers panel.
	"emojiPanBg",
	"emojiPanHeaderBg",
	// Shared media, profile, contacts, onboarding and search results.
	"contactsBg",
	"contactsBgOver",
	"introBg",
	"searchedBarBg",
};

// The whole palette as the style manager saves it, taken before the colors
// were made translucent. Restoring it is the same thing the theme editor does
// when the user reverts a theme, so it puts back the exact colors of the
// current theme, custom ones included.
std::optional<QByteArray> SavedPalette;

// The tint of the material. It follows the theme, so the text stays readable
// while the desktop is still visible through the window.
[[nodiscard]] QColor TintFor(bool night) {
	// The tint sits ON TOP of the material DWM paints, so it has to leave it
	// visible. The previous values (alpha 204 / 208) covered it almost
	// completely: with them the window looked like a flat, slightly
	// see-through panel and the Mica texture was invisible. Unigram uses
	// roughly three quarters of opacity for the same reason.
	return night
		? QColor(32, 32, 32, 170)
		: QColor(246, 246, 246, 175);
}

[[nodiscard]] InterfaceTheme InterfaceThemeFromSettings() {
	return AyuSettings::getInstance().interfaceTheme();
}

[[nodiscard]] bool NightModeFor(InterfaceTheme theme) {
	switch (theme) {
	case InterfaceTheme::Light:
		return false;
	case InterfaceTheme::Dark:
		return true;
	case InterfaceTheme::System:
	default:
		return Platform::SystemDarkTheme();
	}
}

void SwitchNightMode(bool night) {
	if (Window::Theme::IsNightMode() == night) {
		return;
	} else if (!Window::Theme::Background()->nightModeChangeAllowed()) {
		return;
	}
	// ToggleNightMode() flips the current mode and the given path selects the
	// exact theme to apply for the new mode.
	if (night) {
		Window::Theme::ToggleNightMode(Window::Theme::NightThemePath());
	} else {
		Window::Theme::ToggleNightMode(QString());
	}
	Window::Theme::KeepApplied();
}

void SyncSystemWatch(bool enabled) {
	auto &settings = Core::App().settings();
	if (settings.systemDarkModeEnabled() != enabled) {
		settings.setSystemDarkModeEnabled(enabled);
	}
}

void ForEachWindowWidget(Fn<void(not_null<QWidget*>)> callback) {
	Core::App().enumerateWindows([&](not_null<Window::Controller*> window) {
		if (const auto widget = window->widget()) {
			callback(widget);
		}
	});
}

// Watches the activation of a window, because the translucent surface is
// rebuilt with the palette and the palette is global.
class ActivationFilter final : public QObject {
public:
	explicit ActivationFilter(not_null<QWidget*> window)
	: QObject(window) {
		window->installEventFilter(this);
	}

protected:
	bool eventFilter(QObject *object, QEvent *e) override {
		switch (e->type()) {
		case QEvent::WindowActivate:
			SetActive(true);
			break;
		case QEvent::WindowDeactivate:
			SetActive(false);
			break;
		default:
			break;
		}
		return QObject::eventFilter(object, e);
	}

private:
	void SetActive(bool active) {
		if (ActiveWindow == active) {
			return;
		}
		ActiveWindow = active;
		// Only a repaint: the surface and the palette must not change here,
		// or the native window is recreated and the material is lost.
		ForceRepaint();
	}

};

// One filter per window, created on demand. The filter is parented to the
// window, so Qt deletes it together with the window: it must not be owned
// here, or it would be deleted twice. QPointer clears itself when the window
// is gone, so a destroyed window can never be compared against a new one.
std::vector<QPointer<QWidget>> FilteredWindows;

void EnsureActivationFilter(not_null<QWidget*> window) {
	for (const auto &filtered : FilteredWindows) {
		if (filtered == window.get()) {
			return;
		}
	}
	new ActivationFilter(window);
	FilteredWindows.push_back(window.get());
}

} // namespace

bool MicaEnabled() {
	if (!AyuSettings::getInstance().micaEffect()) {
		return false;
	} else if (!Platform::Supported()) {
		return false;
	}
	return Platform::TransparencyEnabled();
}

Platform::Surface WantedSurface() {
	// On Windows 11 22H2 and above the material is painted by DWM, so the
	// window must be an ordinary (not layered) window. Below that build the
	// attribute does not exist and the only thing left is a layered window,
	// which shows the desktop through the window with no material: that is
	// still preferable to an opaque window, but it is not Mica.
	if (!MicaEnabled()) {
		return Platform::Surface::Opaque;
	}
	return Platform::MaterialSupported()
		? Platform::Surface::Material
		: Platform::Surface::Transparent;
}

bool TranslucentWindow() {
	if (!MicaEnabled()) {
		return false;
	}
	// The surface is decided ONCE, when the first window is created, and this
	// is the reason: a layered window and a Mica window are two different
	// windows for DWM. Switching between them means changing
	// WA_TranslucentBackground on a live widget, and Qt reacts by destroying
	// and recreating the native window. Every DWM call made against the old
	// handle still returns S_OK while doing nothing at all, which is exactly
	// how "the windows are transparent but there is no material" happens.
	//
	// The old code switched the surface on every window activation, so the
	// window was recreated on every focus change and the material was lost
	// for good. Now the answer cannot change while the window lives.
	if (!SessionSurfaceDecided) {
		SessionSurfaceDecided = true;
		SessionSurface = WantedSurface();
	}
	if (SessionSurface == Platform::Surface::Opaque) {
		return false;
	}
	// The material needs a surface that is not already filled with a picture:
	// a custom wall paper, a cloud theme or a chat theme keeps its own
	// background, and painting the material behind an image would only make
	// the text unreadable. With the default background of either theme there
	// is nothing to lose, so the material takes over.
	//
	// This is read once, for the same reason as above.
	if (!SessionBackgroundChecked) {
		SessionBackgroundChecked = true;
		SessionDefaultBackground = !Window::Theme::IsNonDefaultBackground();
	}
	return SessionDefaultBackground;
}

QColor WindowBgTint() {
	return TintFor(Window::Theme::IsNightMode());
}

bool ApplyTranslucentPalette(bool themeChanged) {
	const auto wanted = TranslucentWindow();
	const auto applied = SavedPalette.has_value();
	if (themeChanged && applied) {
		// The theme replaced the whole palette, so the backup holds the colors
		// of the previous theme: restoring it now would bring them back over
		// the new ones. It is simply dropped and taken again below, from the
		// palette the new theme just installed.
		SavedPalette.reset();
	} else if (wanted == applied) {
		return false;
	} else if (!wanted) {
		style::main_palette::load(*SavedPalette);
		SavedPalette.reset();
		style::NotifyPaletteChanged();
		return true;
	}
	SavedPalette = style::main_palette::save();
	const auto tint = WindowBgTint();
	for (const auto name : kTranslucentColors) {
		// setColor() exists only with the four channels spelled out: there is
		// no overload taking a QColor in the global palette API (the one that
		// takes a QColor is a method of palette, not of main_palette).
		//
		// The result is Duplicate when the color already has a value, which is
		// exactly what happens here: every one of these colors is set by the
		// theme that is being worn right now.
		[[maybe_unused]] const auto result = style::main_palette::setColor(
			QLatin1String(name),
			uchar(tint.red()),
			uchar(tint.green()),
			uchar(tint.blue()),
			uchar(tint.alpha()));
	}
	style::NotifyPaletteChanged();
	return true;
}

bool PaletteIsTranslucent() {
	return SavedPalette.has_value();
}

void FillBackground(QPainter &p, QRect rect) {
	if (TranslucentWindow()) {
		p.fillRect(rect, WindowBgTint());
	}
}

void UpdateSurface() {
	// The tint follows the theme, so the palette has to be rebuilt when the
	// night mode changed. Nothing else is cached here.
}

void PrepareWindowSurface(not_null<QWidget*> window) {
	// Only the Qt side: the native window does not exist yet, so there is no
	// handle to hand to DWM. ApplyBackdrop() does that afterwards.
	//
	// What matters here is WA_TranslucentBackground:
	//   Material  -> false. The window must stay an ordinary window, because
	//                only an ordinary window gets the DWM material.
	//   Transparent -> true. Classic per-pixel alpha for old Windows builds.
	//   Opaque    -> false.
	// WA_NoSystemBackground keeps Qt from filling the background with a solid
	// colour, which would cover the material.
	const auto surface = WantedSurface();
	const auto transparent = (surface == Platform::Surface::Transparent);
	window->setAttribute(Qt::WA_TranslucentBackground, transparent);
	window->setAttribute(
		Qt::WA_NoSystemBackground,
		(surface != Platform::Surface::Opaque));
	window->setAttribute(
		Qt::WA_OpaquePaintEvent,
		(surface == Platform::Surface::Opaque));
	LOG(("Windows11: window surface prepared, material available: %1."
		).arg(Logs::b(Platform::MaterialSupported())));
}

void ApplyBackdrop(not_null<QWidget*> window) {
	if (!Platform::Supported()) {
		return;
	}
	EnsureActivationFilter(window);

	// The frame theme (dark or light caption) has to be told to DWM: left
	// alone it follows the system theme, so an app forced to dark on a light
	// system would get a light material behind a dark window.
	Platform::ApplyWindowTheme(window, Window::Theme::IsNightMode());

	// TranslucentWindow() freezes the choice on its first call, so reading it
	// here first guarantees that SessionSurface is initialised before it is
	// used, and that the choice never changes afterwards: the native handle is
	// never recreated behind our back.
	const auto translucent = TranslucentWindow();
	Platform::ApplyBackdrop(
		window,
		translucent ? SessionSurface : Platform::Surface::Opaque);
}

void ApplyBackdropToAllWindows() {
	ForEachWindowWidget([](not_null<QWidget*> window) {
		ApplyBackdrop(window);
	});
}

void RefreshWindow(not_null<QWidget*> window) {
	ApplyBackdrop(window);
	Platform::RefreshBackdrop(window);
	window->update();
}

void ApplyInterfaceTheme() {
	const auto theme = InterfaceThemeFromSettings();
	Applied = theme;
	// In the "System" mode the core keeps following the system color scheme.
	SyncSystemWatch(theme == InterfaceTheme::System);
	if (theme != InterfaceTheme::System) {
		SwitchNightMode(NightModeFor(theme));
	}
	Core::App().saveSettingsDelayed();
	HandleThemeUpdate();
}

void ApplyInitialTheme() {
	const auto theme = InterfaceThemeFromSettings();
	Applied = theme;
	SyncSystemWatch(theme == InterfaceTheme::System);
	if (theme != InterfaceTheme::System) {
		SwitchNightMode(NightModeFor(theme));
	}
	UpdateSurface();
}

void ApplyThemeForSystemChange() {
	if (InterfaceThemeFromSettings() != InterfaceTheme::System) {
		return;
	}
	SwitchNightMode(NightModeFor(InterfaceTheme::System));
}

void HandleThemeUpdate(bool themeChanged) {
	UpdateSurface();
	// The palette first: if the colors changed, the windows have to be
	// repainted with the new ones, and the backdrop has to be rebuilt after
	// that. The order matters, otherwise the old colors are on screen for one
	// frame.
	const auto changed = ApplyTranslucentPalette(themeChanged);
	ApplyBackdropToAllWindows();
	if (changed) {
		ForEachWindowWidget([](not_null<QWidget*> window) {
			Platform::RefreshBackdrop(window);
		});
	}
	ForceRepaint();
}

void HandleWindowActivation(bool active) {
	if (ActiveWindow == active) {
		return;
	}
	ActiveWindow = active;
	// The surface does NOT change here. An inactive window keeps its Mica:
	// DWM itself flattens the material of a window it considers inactive, so
	// the only thing to do is let it repaint. Rebuilding the surface here is
	// what used to destroy and recreate the native window on every focus
	// change, losing the material for good.
	ForceRepaint();
}

void ForceRepaint() {
	ForEachWindowWidget([](not_null<QWidget*> window) {
		window->update();
	});
}

void StartWatching() {
	// The system switched between the light and the dark color scheme.
	Core::App().settings().systemDarkModeChanges(
	) | rpl::filter([](std::optional<bool> dark) {
		return dark.has_value();
	}) | rpl::on_next([=] {
		ApplyThemeForSystemChange();
	}, Lifetime);

	// The theme was changed in the AyuGram settings (the settings button only
	// stores the value, so this is the single place that applies it).
	AyuSettings::getInstance().interfaceThemeChanges(
	) | rpl::on_next([=] {
		ApplyInterfaceTheme();
	}, Lifetime);

	// The Mica toggle changed: the surface is fixed for the lifetime of the
	// process, so nothing is rebuilt here. Turning the material on or off
	// takes effect at the next launch (see the note in TranslucentWindow),
	// and the user is told so in the settings description.
	//
	// Rebuilding it live would mean toggling WA_TranslucentBackground on a
	// live widget: Qt destroys and recreates the native window, the backdrop
	// is lost, and the result is a see-through window with no material.
	LOG(("Windows11: the Mica setting changed, the surface is applied on "
		"the next launch."));

	// A wallpaper or a theme was applied, so the palette changed: the
	// translucent colors have to be applied again on top of the new ones.
	Window::Theme::Background()->updates(
	) | rpl::filter([](const Window::Theme::BackgroundUpdate &update) {
		return update.paletteChanged();
	}) | rpl::on_next([=] {
		HandleThemeUpdate(true);
	}, Lifetime);
}

} // namespace AyuFeatures::Windows11
