// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/features/windows11/platform/platform_windows11.h"

namespace AyuFeatures::Windows11::Platform {

bool Supported() {
	return false;
}

bool TransparencyEnabled() {
	return false;
}

bool SystemDarkTheme() {
	return false;
}

bool ApplyBackdrop(not_null<QWidget*>, Backdrop) {
	return false;
}

void ApplyWindowTheme(not_null<QWidget*>, bool) {
}

void ApplyWindowBorderColor(not_null<QWidget*>) {
}

void RefreshBackdrop(not_null<QWidget*>) {
}

} // namespace AyuFeatures::Windows11::Platform
