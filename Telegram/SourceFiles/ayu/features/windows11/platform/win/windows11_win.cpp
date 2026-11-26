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
	return reinterpret_cast<HWND>(widget->winId());
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

bool TransparencyEnabled() {
	auto value = DWORD(1);
	return !ReadPersonalizeDword(L"EnableTransparency", value) || (value != 0);
}

bool SystemDarkTheme() {
	auto value = DWORD(1);
	return ReadPersonalizeDword(L"AppsUseLightTheme", value) && (value == 0);
}

bool ApplyBackdrop(not_null<QWidget*> widget, Backdrop backdrop) {
	if (!Supported()) {
		return false;
	}
	const auto handle = HandleFromWidget(widget);
	if (!handle) {
		return false;
	}

	// A translucent window is a *layered* window: only a layered window has its
	// alpha channel honoured by the compositor, which is what lets the material
	// painted by DWM behind the window show through the pixels Qt paints.
	//
	// Qt sets WS_EX_LAYERED by itself only when Qt::WA_TranslucentBackground is
	// set before the native window exists, which cannot be the case here (the
	// HWND is created before any of this runs), so the bit is changed directly.
	// Without it DWM gets an opaque surface and the backdrop silently does
	// nothing - which is exactly why the first build showed no effect.
	const auto isTranslucent = (backdrop != Backdrop::None);
	const auto style = ::GetWindowLongPtr(handle, GWL_EXSTYLE);
	const auto wanted = isTranslucent
		? (style | WS_EX_LAYERED)
		: (style & ~WS_EX_LAYERED);
	if (wanted != style) {
		::SetWindowLongPtr(handle, GWL_EXSTYLE, wanted);
	}

	// The Qt side has to agree: a widget with WA_OpaquePaintEvent (or with the
	// system background filled in) paints every pixel opaque and the layered
	// window has no alpha to work with.
	if (isTranslucent) {
		widget->setAttribute(Qt::WA_TranslucentBackground, true);
		widget->setAttribute(Qt::WA_NoSystemBackground, true);
		widget->setAttribute(Qt::WA_OpaquePaintEvent, false);
	} else {
		widget->setAttribute(Qt::WA_TranslucentBackground, false);
		widget->setAttribute(Qt::WA_NoSystemBackground, false);
		widget->setAttribute(Qt::WA_OpaquePaintEvent, true);
	}

	// Extending the frame into the whole client area is what makes DWM paint
	// the system material behind the client area and not just the title bar.
	const auto margins = isTranslucent
		? MARGINS{ -1, -1, -1, -1 }
		: MARGINS{ 0, 0, 0, 0 };
	::DwmExtendFrameIntoClientArea(handle, &margins);

	if (BuildNumber() < kBackdropBuild) {
		// Fallback for the original Windows 11 release (21H2): only the
		// undocumented "Mica" flag exists there.
		auto enabled = BOOL(
			backdrop != Backdrop::None && TransparencyEnabled());
		SetAttribute(widget, 1029 /* DWMWA_MICA_EFFECT */, enabled);
		return true;
	}
	auto type = int(backdrop);
	return SetAttribute(widget, DWMWA_SYSTEMBACKDROP_TYPE, type);
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
