#pragma once

class QString;
struct Theme;

// The application stylesheet for 'theme', whose palette must be resolved.
// Requires the themeicon: handler: the sheet refers to tinted glyphs.
[[nodiscard]] QString buildStyleSheet(const Theme& theme);
