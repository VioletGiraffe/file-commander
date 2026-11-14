#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
RESTORE_COMPILER_WARNINGS

// The app's own icons: they replace the OS icons that show no more than whether an object is a folder or a file
enum class FileGlyph { Folder, File };

// A monochrome SVG, for CTintedSvgIconEngine
[[nodiscard]] inline QString fileGlyphResource(const FileGlyph glyph)
{
	return glyph == FileGlyph::Folder ? QStringLiteral(":/theme/folder.svg") : QStringLiteral(":/theme/file.svg");
}
