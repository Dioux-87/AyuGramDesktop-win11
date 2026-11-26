// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/features/windows11/platform/platform_windows11.h"

#ifdef Q_OS_WIN

#include "base/platform/base_platform_info.h"

#include <QtCore/QOperatingSystemVersion>
#include <QtGui/QWindow>

#include <windows.h>
#include <dwmapi.h>

namespace AyuFeatures::Windows11::Platform {
namespace {

// DWMWA_* values introduced in the Windows 11 SDK. Defining them locally keeps
// the build compatible with older SDKs.
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif // DWMWA_USE_IMMERSIVE_DARK_MODE
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif // DWMWA_WINDOW_CORNER_PREFERENCE
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif // DWMWA_BORDER_COLOR
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif // DWMWA_CAPTION_COLOR
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif // DWMWA_SYSTEMBACKDROP_TYPE

#ifndef DWMWCP_DEFAULT
#define DWMWCP_DEFAULT 0
#endif // DWMWCP_DEFAULT
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif // DWMWCP_ROUND

// DWMWA_SYSTEMBACKDROP_TYPE (Mica / Acrylic / Tabbed) is available since
// Windows 11 22H2 (build 22621).
constexpr auto kBackdropBuild = 22621;

[[nodiscard]] DWORD BuildNumber() {
	static const auto result = [] {
		const auto version = QOperatingSystemVersion::current();
		if (version.majorVersion() < 10) {
			return DWORD(0);
		}
		return DWORD(version.microVersion());
	}();
	return result;
}

[[nodiscard]] HWND HandleFromWidget(not_null<QWidget*> widget) {
	// winId() is deliberately NOT used here.
	//
	// QWidget::winId() *creates* the native window when it does not exist yet.
	// On Windows the platform plugin may then have to recreate it (for example
	// after the widget attributes change), and the recreation destroys the
	// handle DWM was configured for. Every DwmSetWindowAttribute call against
	// the destroyed handle still returns S_OK while doing nothing at all.
	//
	// Reading the handle only from a window that already exists avoids that
	// completely: windowHandle() is not null only once the native window is
	// there, and internalWinId() never creates anything.
	const auto window = widget->windowHandle();
	if (window) {
		return reinterpret_cast<HWND>(window->winId());
	}
	return reinterpret_cast<HWND>(widget->internalWinId());
}

template <typename Value>
bool SetAttribute(not_null<QWidget*> widget, DWORD attribute, Value value) {
	const auto handle = HandleFromWidget(widget);
	if (!handle) {
		return false;
	}
	return SUCCEEDED(::DwmSetWindowAttribute(
		handle,
		attribute,
		&value,
		sizeof(value)));
}

template <typename Name>
bool ReadPersonalizeDword(Name name, DWORD &out) {
	const auto keyName = L""
		"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
	auto key = HKEY();
	if (::RegOpenKeyEx(HKEY_CURRENT_USER, keyName, 0, KEY_READ, &key)
		!= ERROR_SUCCESS) {
		return false;
	}
	auto type = DWORD(0);
	auto size = DWORD(sizeof(out));
	const auto result = ::RegQueryValueEx(
		key,
		name,
		0,
		&type,
		reinterpret_cast<LPBYTE>(&out),
		&size);
	::RegCloseKey(key);
	return (result == ERROR_SUCCESS) && (type == REG_DWORD);
}

} // namespace

bool Supported() {
	return ::Platform::IsWindows11OrGreater();
}

bool MaterialSupported() {
	return Supported() && (BuildNumber() >= kBackdropBuild);
}

bool TransparencyEnabled() {
	auto value = DWORD(1);
	return !ReadPersonalizeDword(L"EnableTransparency", value) || (value != 0);
}

bool SystemDarkTheme() {
	auto value = DWORD(1);
	return ReadPersonalizeDword(L"AppsUseLightTheme", value) && (value == 0);
}

bool ApplyBackdrop(not_null<QWidget*> widget, Surface surface) {
	if (!Supported()) {
		return false;
	}
	const auto handle = HandleFromWidget(widget);
	if (!handle) {
		return false;
	}

	// ---------------------------------------------------------------------
	// THE ONE THING THAT MATTERS: a layered window never gets a material.
	// ---------------------------------------------------------------------
	// DWM paints Mica for a window that it treats as an ordinary opaque
	// window whose frame was extended into the client area. If the window
	// carries WS_EX_LAYERED (per-pixel alpha), DWM composites the window over
	// whatever is behind it instead of painting the material: the result is a
	// see-through window with no blur, no tint and no texture, which looks
	// broken rather than like Mica.
	//
	// The two previous builds set WS_EX_LAYERED *and* asked for a backdrop,
	// so they asked for two incompatible compositions. The material was
	// requested and silently never painted.
	//
	// So: WS_EX_LAYERED goes away for Material. It stays only for the
	// Transparent fallback, and it is decided once, when the native window is
	// created, never toggled later: Qt destroys and recreates the HWND when
	// WA_TranslucentBackground changes on a live window, and every DWM call
	// made against the old handle silently does nothing.
	const auto wantsMaterial = (surface == Surface::Material);
	const auto wantsTransparent = (surface == Surface::Transparent);

	auto style = ::GetWindowLongPtr(handle, GWL_EXSTYLE);
	if (wantsMaterial || surface == Surface::Opaque) {
		style &= ~WS_EX_LAYERED;
	} else if (wantsTransparent) {
		style |= WS_EX_LAYERED;
	}
	::SetWindowLongPtr(handle, GWL_EXSTYLE, style);

	// Qt has to agree with the composition:
	//  - Material: the widget must NOT be translucent for the native window
	//    (that is what creates the layered window), but it must not paint an
	//    opaque background either, otherwise the material is covered by a
	//    solid rectangle. WA_NoSystemBackground + no OpaquePaintEvent is the
	//    combination: Qt paints only what the widgets draw, and the tint
	//    painted by MainWidget::paintEvent fills the rest.
	//  - Transparent: per-pixel alpha is exactly what we want.
	//  - Opaque: everything normal.
	widget->setAttribute(
		Qt::WA_TranslucentBackground,
		wantsTransparent);
	widget->setAttribute(
		Qt::WA_NoSystemBackground,
		wantsMaterial || wantsTransparent);
	widget->setAttribute(
		Qt::WA_OpaquePaintEvent,
		surface == Surface::Opaque);

	// Extending the frame into the whole client area is what makes DWM paint
	// the material behind the client area and not just behind the title bar.
	const auto margins = (wantsMaterial || wantsTransparent)
		? MARGINS{ -1, -1, -1, -1 }
		: MARGINS{ 0, 0, 0, 0 };
	::DwmExtendFrameIntoClientArea(handle, &margins);

	auto result = true;
	if (!MaterialSupported()) {
		// Windows 11 21H2: only the undocumented "Mica" boolean exists.
		auto enabled = BOOL(wantsMaterial && TransparencyEnabled());
		result = SetAttribute(widget, 1029 /* DWMWA_MICA_EFFECT */, enabled);
	} else {
		const auto type = int(wantsMaterial
			? Backdrop::Mica
			: Backdrop::None);
		result = SetAttribute(widget, DWMWA_SYSTEMBACKDROP_TYPE, type);
	}

	// DWM does not always pick the attribute up until the non-client area is
	// recomputed. A no-op SetWindowPos with SWP_FRAMECHANGED is the documented
	// way to force that.
	if (wantsMaterial) {
		::SetWindowPos(
			handle,
			nullptr,
			0,
			0,
			0,
			0,
			SWP_FRAMECHANGED
				| SWP_NOMOVE
				| SWP_NOSIZE
				| SWP_NOZORDER
				| SWP_NOACTIVATE
				| SWP_NOOWNERZORDER);
	}
	return result;
}

void ApplyWindowTheme(not_null<QWidget*> widget, bool dark) {
	if (!Supported()) {
		return;
	}
	// Which tint DWM draws the material in. It has to be told: left alone it
	// follows the system theme, so an app forced to dark on a light system
	// would get a light material behind a dark window.
	auto darkValue = BOOL(dark ? TRUE : FALSE);
	SetAttribute(widget, DWMWA_USE_IMMERSIVE_DARK_MODE, darkValue);

	// The rounded corners are drawn by DWM; ask for the default (rounded)
	// preference so that a custom translucent window still looks native.
	auto corners = int(DWMWCP_ROUND);
	SetAttribute(widget, DWMWA_WINDOW_CORNER_PREFERENCE, corners);
}

void ApplyWindowBorderColor(not_null<QWidget*> widget) {
	if (!Supported()) {
		return;
	}
	// Only the border: DWMWA_COLOR_NONE keeps the system default, which
	// follows the user accent color when "Show accent color on title bars" is
	// enabled.
	//
	// The caption color is deliberately left alone: lib_ui already sets it to
	// the title bar color of the current theme (see ui_window_win.cpp), and
	// overwriting it here is what made the window frame lighter than the
	// window contents.
	constexpr auto kColorNone = DWORD(0xFFFFFFFE);
	SetAttribute(widget, DWMWA_BORDER_COLOR, kColorNone);
}

void RefreshBackdrop(not_null<QWidget*> widget) {
	if (!Supported()) {
		return;
	}
	const auto handle = HandleFromWidget(widget);
	if (!handle) {
		return;
	}
	// Forces DWM to recompute the material with the new system colors.
	::SetWindowPos(
		handle,
		nullptr,
		0,
		0,
		0,
		0,
		SWP_FRAMECHANGED
			| SWP_NOMOVE
			| SWP_NOSIZE
			| SWP_NOZORDER
			| SWP_NOACTIVATE
			| SWP_NOOWNERZORDER);
	::RedrawWindow(
		handle,
		nullptr,
		nullptr,
		RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
}

} // namespace AyuFeatures::Windows11::Platform

#endif // Q_OS_WIN
