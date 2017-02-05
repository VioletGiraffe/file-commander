#include "cfilecomparisonplugin.h"

#include "assert/advanced_assert.h"
#include "utility/on_scope_exit.hpp"

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
	const QString otherFilePath = otherItem.isFile() ? otherItem.fullAbsolutePath() : _proxy->currentFolderPathForPanel(_proxy->otherPanel()) + "/" + currentItem.fullName();

	QFile fileA(currentItem.fullAbsolutePath()), fileB(otherFilePath);
	if (!fileA.exists() || !fileB.exists())
	{
		QMessageBox::information(nullptr, "No file selected", "No file is selected for comparison.");
		return;
	}

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

	if (compareFilesByContents(fileA, fileB, [](int) {}) == Equal)
		QMessageBox::information(nullptr, "Files are identical", QObject::tr("The file %1 is identical in both locations.").arg(currentItem.fullName()));
	else
		QMessageBox::information(nullptr, "Files differ", "The files are not identical.");
}

ComparisonResult compareFilesByContents(QFile& fileA, QFile& fileB, std::function<void(int)> progressCallback)
{
	assert(progressCallback);

	EXEC_ON_SCOPE_EXIT([&]() {progressCallback(100); });

	constexpr int blockSize = 512 * 1024 * 1024;

	QByteArray blockA(blockSize, Qt::Uninitialized), blockB(blockSize, Qt::Uninitialized);

	for (qint64 pos = 0, size = fileA.size(); pos < size; pos += blockSize)
	{
		const auto block_a_size = fileA.read(blockA.data(), blockSize);
		const auto block_b_size = fileB.read(blockB.data(), blockSize);

		assert_and_return_r(block_a_size == blockSize || block_a_size == block_b_size, NotEqual);
		assert_and_return_r(block_b_size == blockSize || block_a_size == block_b_size, NotEqual);

		if (blockA != blockB)
			return NotEqual;

		progressCallback(static_cast<int>(pos * 100 / size));
	}

	return Equal;
}
