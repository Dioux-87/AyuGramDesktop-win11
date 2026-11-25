// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#pragma once

#include "base/basic_types.h"

class QColor;
class QPainter;
class QRect;
class QWidget;

namespace AyuFeatures::Windows11 {

// Whether the Windows 11 Mica backdrop can be used right now: the feature is
// enabled in the AyuGram settings, the OS supports it and the user keeps the
// system transparency effects enabled.
[[nodiscard]] bool MicaEnabled();

// Whether the window background must be translucent, so the system material
// behind the window is visible. A custom chat wallpaper always wins.
[[nodiscard]] bool TranslucentWindow();

// The translucent tint painted over the Mica backdrop for the current theme.
[[nodiscard]] QColor WindowBgTint();

// Paints the translucent Mica tint over the given rectangle. Does nothing
// when the window is opaque, so callers can always call it before their own
// painting.
void FillBackground(QPainter &p, QRect rect);

// Recomputes the cached surface state.
void UpdateSurface();

// Applies (or removes) the backdrop and the matching window frame theme.
void ApplyBackdrop(not_null<QWidget*> window);

// Re-applies the backdrop to every opened window.
void ApplyBackdropToAllWindows();

// Rebuilds the backdrop and repaints the contents of the given window.
void RefreshWindow(not_null<QWidget*> window);

// Applies the Light / Dark / System choice from the AyuGram settings.
void ApplyInterfaceTheme();

// Picks the theme right after the background was started, before any window
// is created.
void ApplyInitialTheme();

// Re-picks the theme after the system color scheme changed.
void ApplyThemeForSystemChange();

// Recomputes the surface and the backdrop of all windows after the palette,
// the wallpaper or the settings changed.
void HandleThemeUpdate();

// Forces a full repaint of every opened window.
void ForceRepaint();

// Installs the theme watchers (system color scheme and AyuGram settings).
void StartWatching();

} // namespace AyuFeatures::Windows11
