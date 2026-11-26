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

// How DWM should compose the window.
//
// This distinction is the whole trick, and getting it wrong is what made two
// builds show a see-through window with no material in it:
//
//   Material
//       The window is NOT layered and has NO per-pixel alpha. DWM paints the
//       Mica material itself, over the whole window because the frame is
//       extended into the client area. Qt paints a translucent tint on top of
//       it, so the material shows through the tint. This is what Unigram and
//       every native Windows 11 app do.
//
//   Transparent
//       The window is a classic layered window (WS_EX_LAYERED, per-pixel
//       alpha). DWM does not paint any material: everything that is not
//       painted shows whatever is behind the window, unfiltered. It is only
//       a fallback for a build of Windows older than 22H2, where Mica does
//       not exist at all.
enum class Surface : int {
	Opaque = 0,
	Material = 1,
	Transparent = 2,
};

// Whether the current OS exposes the Windows 11 backdrop API.
[[nodiscard]] bool Supported();

// Whether this Windows build can paint the Mica material at all. Below
// Windows 11 22H2 (build 22621) the attribute is silently ignored, so a
// window with per-pixel alpha would show the desktop through it instead of a
// material: that is the "transparent but flat, no blur" look.
[[nodiscard]] bool MaterialSupported();

// Whether the system transparency effects are enabled by the user.
[[nodiscard]] bool TransparencyEnabled();

// Whether the system is currently using the dark app theme.
[[nodiscard]] bool SystemDarkTheme();

// Composes the window for the given surface and asks DWM for the given
// backdrop. This must be called at most once per window, with the native
// window already created, and the surface must not change while the window
// lives (see the note about layered windows in windows11_win.cpp).
bool ApplyBackdrop(not_null<QWidget*> widget, Surface surface);

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
