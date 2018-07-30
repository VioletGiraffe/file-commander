#include "cfilecomparisonplugin.h"

#include "assert/advanced_assert.h"

DISABLE_COMPILER_WARNINGS
#include <QDebug>
#include <QFile>
#include <QMessageBox>
#include <QProgressDialog>
RESTORE_COMPILER_WARNINGS

CFileCommanderPlugin* createPlugin()
{
	return new CFileComparisonPlugin;
}

QString CFileComparisonPlugin::name() const
{
	return QObject::tr("File comparison plugin");
}

void CFileComparisonPlugin::proxySet()
{
	CPluginProxy::MenuTree menu(QObject::tr("Compare files by contents"), [this]() {
		compareSelectedFiles();
	});

	_proxy->createToolMenuEntries(menu);
}

void CFileComparisonPlugin::compareSelectedFiles()
{
	const auto& currentItem = _proxy->currentItemForPanel(_proxy->currentPanel());
	if (!currentItem.isFile())
	{
		QMessageBox::information(nullptr, QObject::tr("No file selected"), QObject::tr("No file is selected for comparison."));
		return;
	}

	const auto& otherItem = _proxy->currentItemForPanel(_proxy->otherPanel());
	const QString otherFilePath = otherItem.isFile() ? otherItem.fullAbsolutePath() : _proxy->currentFolderPathForPanel(_proxy->otherPanel()) + "/" + currentItem.fullName();

	QFile fileA(currentItem.fullAbsolutePath()), fileB(otherFilePath);
	if (!fileA.exists() || !fileB.exists())
	{
		QMessageBox::information(nullptr, QObject::tr("No file selected"), QObject::tr("No file is selected for comparison."));
		return;
	}

	if (fileA.size() != fileB.size())
	{
		const QString msg = QObject::tr("Files have different sizes:\n%1: %2\n%3: %4").arg(currentItem.fullAbsolutePath()).arg(fileA.size()).arg(otherFilePath).arg(fileB.size());
		QMessageBox::information(nullptr, QObject::tr("Files differ"), msg);
		return;
	}

	if (!fileA.open(QFile::ReadOnly))
	{
		QMessageBox::warning(nullptr, QObject::tr("Failed to read file"), QObject::tr("Failed to open file") + fileA.fileName());
		return;
	}

	if (!fileB.open(QFile::ReadOnly))
	{
		QMessageBox::warning(nullptr, QObject::tr("Failed to read file"), QObject::tr("Failed to open file") + fileB.fileName());
		return;
	}


	QProgressDialog progressDialog;

	_comparator.compareFilesThreaded(fileA, fileB, [](int p) {qInfo() << p;}, [](CFileComparator::ComparisonResult result) {});

// 	if (compareFilesByContents(fileA, fileB, [](int p) {qInfo() << p;}) == Equal)
// 		QMessageBox::information(nullptr, QObject::tr("Files are identical"), QObject::tr("The file %1 is identical in both locations.").arg(currentItem.fullName()));
// 	else
// 		QMessageBox::information(nullptr, QObject::tr("Files differ"), QObject::tr("The files are not identical."));
}
