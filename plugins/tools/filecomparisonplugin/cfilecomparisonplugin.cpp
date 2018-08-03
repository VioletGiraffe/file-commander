#include "cfilecomparisonplugin.h"
#include "plugininterface/cpluginproxy.h"
#include "assert/advanced_assert.h"

DISABLE_COMPILER_WARNINGS
#include <QFile>
#include <QMessageBox>
RESTORE_COMPILER_WARNINGS

#include <limits>
#include <memory>
#include <utility> // std::move

CFileCommanderPlugin* createPlugin()
{
	return new CFileComparisonPlugin;
}

CFileComparisonPlugin::CFileComparisonPlugin()
{
	_progressDialog.setLabelText("Comparing the selected files...");
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
		QMessageBox::information(nullptr, name(), QObject::tr("No file is selected for comparison."));
		return;
	}

	const auto& otherItem = _proxy->currentItemForPanel(_proxy->otherPanel());
	const auto fileName = currentItem.fullName();
	const QString otherFilePath = otherItem.isFile() ? otherItem.fullAbsolutePath() : _proxy->currentFolderPathForPanel(_proxy->otherPanel()) + "/" + fileName;

	auto fileA = std::make_unique<QFile>(currentItem.fullAbsolutePath());
	auto fileB = std::make_unique<QFile>(otherFilePath);

	if (!fileA->exists() || !fileB->exists())
	{
		QMessageBox::information(nullptr, name(), QObject::tr("No file is selected for comparison."));
		return;
	}

	if (fileA->size() != fileB->size())
	{
		const QString msg = QObject::tr("Files have different sizes:\n%1: %2\n%3: %4").arg(currentItem.fullAbsolutePath()).arg(fileA->size()).arg(otherFilePath).arg(fileB->size());
		QMessageBox::information(nullptr, name(), msg);
		return;
	}

	if (!fileA->open(QFile::ReadOnly))
	{
		QMessageBox::warning(nullptr, name(), QObject::tr("Failed to open file") % ' ' % fileA->fileName());
		return;
	}

	if (!fileB->open(QFile::ReadOnly))
	{
		QMessageBox::warning(nullptr, name(), QObject::tr("Failed to open file") % ' ' % fileB->fileName());
		return;
	}

	_progressDialog.show();
	_progressDialog.adjustSize();

	_comparator.compareFilesThreaded(std::move(fileA), std::move(fileB),
		[this](int progressPercentage) {
			_proxy->execOnUiThread([this, progressPercentage]() {
				_progressDialog.setValue(progressPercentage);
			});
		},

		[fileName, this](CFileComparator::ComparisonResult result) {
			_proxy->execOnUiThread([this, result, fileName]() {
				_progressDialog.hide();

				if (result == CFileComparator::Equal)
					QMessageBox::information(nullptr, QObject::tr("Files are identical"), QObject::tr("The file %1 is identical in both locations.").arg(fileName));
				else if (result == CFileComparator::NotEqual)
					QMessageBox::information(nullptr, QObject::tr("Files differ"), QObject::tr("The files are not identical."));
			});
		}
	);
}
