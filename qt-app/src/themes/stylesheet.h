#pragma once

#include "diskenumerator/volumeinfo.hpp"

class QString;
struct Theme;

// The application stylesheet for 'theme', whose palette must be resolved.
// Requires the themeicon: handler: the sheet refers to tinted glyphs.
[[nodiscard]] QString buildStyleSheet(const Theme& theme);

// A drive button with a glyph has this property, set to driveGlyphName(): the sheet draws the glyph in the button's text colour.
inline constexpr char driveGlyphProperty[] = "driveGlyph";
// The theme glyph for a drive of this kind. Empty: none.
[[nodiscard]] QString driveGlyphName(VolumeInfo::Kind kind);
