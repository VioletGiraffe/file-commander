#include "cfilecomparisonplugin.h"

DISABLE_COMPILER_WARNINGS
#include <QFile>
#include <QMessageBox>
RESTORE_COMPILER_WARNINGS

enum ComparisonResult { Equal, NotEqual };
static ComparisonResult compareFilesByContents(QFile& fileA, QFile& fileB, std::function<void(int)> progressCallback);

CFileCommanderPlugin* createPlugin()
{
	return new CFileComparisonPlugin;
}

CFileComparisonPlugin::CFileComparisonPlugin()
{
}

QString CFileComparisonPlugin::name() const
{
	return QObject::tr("File comparison plugin");
}

void CFileComparisonPlugin::proxySet()
{
	CPluginProxy::MenuTree menu("Compare files by contents", [this]() {
		compareSelectedFiles();
	});

	_proxy->createToolMenuEntries(menu);
}

void CFileComparisonPlugin::compareSelectedFiles()
{
	const auto& currentItem = _proxy->currentItemForPanel(_proxy->currentPanel());
	if (!currentItem.isFile())
	{
		QMessageBox::information(nullptr, "No file selected", "No file is selected for comparison.");
		return;
	}

	const auto& otherItem = _proxy->currentItemForPanel(_proxy->otherPanel());
	const QString otherFilePath = otherItem.isFile() ? otherItem.fullAbsolutePath() : otherItem.parentDirPath() + "/" + currentItem.fullName();

	QFile fileA(currentItem.fullAbsolutePath()), fileB(otherFilePath);
	if (fileA.size() != fileB.size())
	{
		const QString msg = QString("Files have different sizes:\n%1: %2\n%3: %4").arg(currentItem.fullAbsolutePath()).arg(fileA.size()).arg(otherFilePath).arg(fileB.size());
		QMessageBox::information(nullptr, "Files differ", msg);
		return;
	}

	if (!fileA.open(QFile::ReadOnly))
	{
		QMessageBox::warning(nullptr, "Failed to read file", "Failed to open file" + fileA.fileName());
		return;
	}

	if (!fileB.open(QFile::ReadOnly))
	{
		QMessageBox::warning(nullptr, "Failed to read file", "Failed to open file" + fileB.fileName());
		return;
	}

	if (compareFilesByContents(fileA, fileB, std::function<void(int)>()) == Equal)
		QMessageBox::information(nullptr, "Files are identical", "The files are identical.");
	else
		QMessageBox::information(nullptr, "Files differ", "The files are not identical.");
}

ComparisonResult compareFilesByContents(QFile& fileA, QFile& fileB, std::function<void(int)> progressCallback)
{
	constexpr int blockSize = 512 * 1024 * 1024;

	for (qint64 pos = 0, size = fileA.size(); pos < size; pos += blockSize)
	{
		const QByteArray blockA = fileA.read(blockSize), blockB = fileB.read(blockSize);
		if (blockA != blockB)
			return NotEqual;

		progressCallback(static_cast<int>(pos * 100 / size));
	}

	progressCallback(100);
	return Equal;
}
