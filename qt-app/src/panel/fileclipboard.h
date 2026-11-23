#pragma once

#include "cfilesystemobject.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"
#include "fileoperations/usermessage.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
#include <qnamespace.h>
RESTORE_COMPILER_WARNINGS

#include <optional>
#include <vector>

class QImage;
class QMimeData;

// Puts the files on the clipboard: through the shell on Windows, as file URLs elsewhere
// cut: a paste moves them instead of copying.
void putFilesOnClipboard(const std::vector<CFileSystemObject>& files, bool cut);

#ifndef _WIN32
// A move for files that putFilesOnClipboard() put there cut, otherwise a copy
[[nodiscard]] Qt::DropAction clipboardFilesDropAction(const QMimeData& mimeData);
#endif

// Saves the image in the folder as clipboard.png, or clipboard_N.png when that is taken; lossy: JPEG instead of PNG
// Returns the message for the user on failure
[[nodiscard]] std::optional<UserMessage> saveImageAsNewFile(const QImage& image, const QString& folderPath, bool lossy);
