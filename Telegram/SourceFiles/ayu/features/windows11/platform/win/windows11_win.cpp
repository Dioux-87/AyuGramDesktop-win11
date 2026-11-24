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
	return SUCCEEDED(::DwmSetWindowAttribute(
		HandleFromWidget(widget),
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
	// The window must be transparent for DWM to show the backdrop through it.
	widget->setAttribute(Qt::WA_NoSystemBackground, true);
	widget->setAttribute(Qt::WA_TranslucentBackground, true);

	// Extending the frame into the whole client area makes DWM paint the
	// system material behind the window content.
	const auto margins = MARGINS{ -1, -1, -1, -1 };
	::DwmExtendFrameIntoClientArea(HandleFromWidget(widget), &margins);

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
	// DWMWA_COLOR_NONE keeps the system default border, which follows the
	// user accent color when "Show accent color on title bars" is enabled.
	constexpr auto kColorNone = DWORD(0xFFFFFFFE);
	SetAttribute(widget, DWMWA_BORDER_COLOR, kColorNone);

	if (BuildNumber() >= kBackdropBuild) {
		SetAttribute(widget, DWMWA_CAPTION_COLOR, kColorNone);
	}
}

void RefreshBackdrop(not_null<QWidget*> widget) {
	if (!Supported()) {
		return;
	}
	const auto handle = HandleFromWidget(widget);
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
