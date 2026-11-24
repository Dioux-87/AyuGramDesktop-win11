// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "base/basic_types.h"

class QWidget;

namespace AyuFeatures::Windows11::Platform {

// Backdrop modes exposed by DWM on Windows 11 22H2 and above.
// Values match DWM_SYSTEMBACKDROP_TYPE from dwmapi.h.
enum class Backdrop : int {
	Auto = 0,
	None = 1,
	Mica = 2,
	Acrylic = 3,
	Tabbed = 4,
};

// Whether the current OS exposes the Windows 11 backdrop API.
[[nodiscard]] bool Supported();

// Whether the system transparency effects are enabled by the user.
[[nodiscard]] bool TransparencyEnabled();

// Whether the system is currently using the dark app theme.
[[nodiscard]] bool SystemDarkTheme();

// Applies the given backdrop type to the window. Returns false when the
// platform doesn't support it (another OS or an older Windows build).
bool ApplyBackdrop(not_null<QWidget*> widget, Backdrop backdrop);

// Sets the rounded corners / border color preferences of the window so the
// window frame matches the in-app theme.
void ApplyWindowTheme(not_null<QWidget*> widget, bool dark);

// Applies the system accent color to the window border and title bar,
// used by the Windows 11 style frame.
void ApplyWindowBorderColor(not_null<QWidget*> widget);

// Asks DWM to rebuild the backdrop after a theme (light/dark) change so the
// tint of the Mica material follows the new system palette.
void RefreshBackdrop(not_null<QWidget*> widget);

} // namespace AyuFeatures::Windows11::Platform
