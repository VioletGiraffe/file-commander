#include "fileclipboard.h"

#include "filesystemhelperfunctions.h"
#include "shell/cshell.h"


// Submodule includes
#include "assert/advanced_assert.h"
#include "qtcore_helpers/qstring_helpers.hpp"


DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QImage>
#include <QImageWriter>
#include <QMimeData>
#include <QUrl>
RESTORE_COMPILER_WARNINGS

#include <string>

#ifndef _WIN32
// Set on the clipboard's data by putFilesOnClipboard()
static constexpr char cutFilesProperty[] = "cut";
#endif

void putFilesOnClipboard(const std::vector<CFileSystemObject>& files, const bool cut)
{
#ifndef _WIN32
	QList<QUrl> urls;
	urls.reserve((qsizetype)files.size());
	for (const CFileSystemObject& file : files)
		urls.push_back(QUrl::fromLocalFile(file.fullAbsolutePath()));

	auto* mime = new QMimeData;
	mime->setUrls(urls);
	mime->setProperty(cutFilesProperty, cut);
	QApplication::clipboard()->setMimeData(mime);
#else
	std::vector<std::wstring> paths;
	paths.reserve(files.size());
	for (const CFileSystemObject& file : files)
		paths.push_back(toNativeSeparators(file.fullAbsolutePath()).toStdWString());

	OsShell::putObjectsOnClipboard(paths, cut);
#endif
}

#ifndef _WIN32
Qt::DropAction clipboardFilesDropAction(const QMimeData& mimeData)
{
	return mimeData.property(cutFilesProperty).toBool() ? Qt::MoveAction : Qt::CopyAction;
}
#endif

std::optional<UserMessage> saveImageAsNewFile(const QImage& image, const QString& folderPath, const bool lossy)
{
	assert_r(folderPath.endsWith(nativeSeparator()));

	// Concatenated, not QString::arg: a %N inside the directory path would be substituted
	const QString extension = lossy ? QSL(".jpg") : QSL(".png");
	QString imagePath = folderPath + QSL("clipboard") + extension;
	for (int i = 1; QFile::exists(imagePath); ++i)
		imagePath = folderPath + QSL("clipboard_") + QString::number(i) + extension;

	QImageWriter writer(imagePath, lossy ? "jpg" : "png");
	if (lossy)
		writer.setQuality(80);
	else
		writer.setCompression(100); // Maximum zlib compression level for PNG; lossless, so there's no quality trade-off.

	if (writer.write(image))
		return {};

	// A failed write can leave a partial file under the name that was free.
	// The writer holds its file open until destroyed: Windows cannot remove an open file.
	writer.device()->close();
	QFile::remove(imagePath);
	return UserMessage{ MessageSeverity::Error, QObject::tr("Failed to paste the image"), QObject::tr("Failed to save the image as %1: %2").arg(imagePath, writer.errorString()) };
}
