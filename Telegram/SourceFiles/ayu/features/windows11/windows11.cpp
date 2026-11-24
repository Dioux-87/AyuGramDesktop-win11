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
#include "data/data_wall_paper.h"
#include "core/core_settings.h"
#include "window/themes/window_theme.h"
#include "window/window_controller.h"

#include <QtGui/QColor>
#include <QtGui/QPainter>

namespace AyuFeatures::Windows11 {
namespace {

rpl::lifetime Lifetime;

InterfaceTheme Applied;

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

} // namespace

bool MicaEnabled() {
	if (!AyuSettings::getInstance().micaEffect()) {
		return false;
	} else if (!Platform::Supported()) {
		return false;
	}
	return Platform::TransparencyEnabled();
}

bool UsesDefaultWallPaper() {
	const auto &paper = Window::Theme::Background()->paper();
	// The light "day" wall paper is painted as a flat color, so the Mica
	// material can take over. Everything else (a theme background, any custom
	// or cloud wall paper and the default night wall paper) is an image, so
	// the system material stays behind an opaque surface.
	return Data::IsDefaultWallPaper(paper)
		&& !Window::Theme::IsNightMode();
}

bool TranslucentWindow() {
	if (!MicaEnabled()) {
		return false;
	}
	return UsesDefaultWallPaper();
}

QColor WindowBgTint() {
	const auto night = Window::Theme::IsNightMode();
	const auto base = night ? QColor(32, 32, 32) : QColor(243, 243, 243);
	// The tint keeps the text readable while still letting the desktop show
	// through the window.
	const auto alpha = night ? 200 : 216;
	return QColor(base.red(), base.green(), base.blue(), alpha);
}

void FillBackground(QPainter &p, QRect rect) {
	if (TranslucentWindow()) {
		p.fillRect(rect, WindowBgTint());
	}
}

void UpdateSurface() {
}

void ApplyBackdrop(not_null<QWidget*> window) {
	if (!Platform::Supported()) {
		return;
	}
	Platform::ApplyWindowTheme(window, Window::Theme::IsNightMode());
	Platform::ApplyWindowBorderColor(window);

	const auto translucent = TranslucentWindow();
	Platform::ApplyBackdrop(
		window,
		translucent ? Platform::Backdrop::Mica : Platform::Backdrop::None);
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

void HandleThemeUpdate() {
	UpdateSurface();
	ApplyBackdropToAllWindows();
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

	// The Mica toggle was switched on or off.
	AyuSettings::getInstance().micaEffectChanges(
	) | rpl::skip(1) | rpl::on_next([=] {
		HandleThemeUpdate();
	}, Lifetime);

	// A wallpaper or a theme was applied, so the palette changed.
	Window::Theme::Background()->updates(
	) | rpl::filter([](const Window::Theme::BackgroundUpdate &update) {
		return update.paletteChanged();
	}) | rpl::on_next([=] {
		HandleThemeUpdate();
	}, Lifetime);
}

} // namespace AyuFeatures::Windows11
