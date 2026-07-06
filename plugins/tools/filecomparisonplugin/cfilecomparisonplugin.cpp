#include "cfilecomparisonplugin.h"
#include "plugininterface/cpluginproxy.h"

DISABLE_COMPILER_WARNINGS
#include <QFile>
#include <QMessageBox>
RESTORE_COMPILER_WARNINGS

#include <memory>
#include <utility> // std::move

CFileCommanderPlugin* createPlugin()
{
	return new CFileComparisonPlugin;
}

CFileComparisonPlugin::CFileComparisonPlugin() noexcept
{
	_progressDialog.setLabelText(QObject::tr("Comparing the selected files..."));
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
		QMessageBox::warning(nullptr, name(), QObject::tr("The selected item is not a file:\n") + currentItem.fullAbsolutePath());
		return;
	}

	const auto& otherItem = _proxy->currentItemForPanel(_proxy->otherPanel());
	const auto fileName = currentItem.fullName();
	const QString otherFilePath = otherItem.isFile() ? otherItem.fullAbsolutePath() : _proxy->currentFolderPathForPanel(_proxy->otherPanel()) + "/" + fileName;

	auto fileA = std::make_unique<QFile>(currentItem.fullAbsolutePath());
	auto fileB = std::make_unique<QFile>(otherFilePath);

	if (!fileA->exists())
	{
		QMessageBox::warning(nullptr, name(), QObject::tr("The file\n%1\nis selected for comparison, but doesn't exist.").arg(fileA->fileName()));
		return;
	}

	if (!fileB->exists())
	{
		QMessageBox::warning(nullptr, name(), QObject::tr("The file\n%1\nis selected for comparison, but doesn't exist.").arg(fileB->fileName()));
		return;
	}

	if (fileA->size() != fileB->size())
	{
		const QString msg = QObject::tr("Files have different sizes:\n%1: %2\n%3: %4").arg(currentItem.fullAbsolutePath()).arg(fileA->size()).arg(otherFilePath).arg(fileB->size());
		QMessageBox::warning(nullptr, name(), msg);
		return;
	}

	if (!fileA->open(QFile::ReadOnly))
	{
		QMessageBox::critical(nullptr, name(), QObject::tr("Failed to open file") % ' ' % fileA->fileName());
		return;
	}

	if (!fileB->open(QFile::ReadOnly))
	{
		QMessageBox::critical(nullptr, name(), QObject::tr("Failed to open file") % ' ' % fileB->fileName());
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

		[filePathA{ currentItem.fullAbsolutePath() }, filePathB{ otherFilePath }, this](CFileComparator::ComparisonResult result) {
			_proxy->execOnUiThread([this, result, filePathA, filePathB]() {
				_progressDialog.hide();

				if (result == CFileComparator::Equal)
					QMessageBox::information(nullptr, QObject::tr("Files are identical"), QObject::tr("The file\n%1\n\nis IDENTICAL to\n\n%2.").arg(filePathA, filePathB));
				else if (result == CFileComparator::NotEqual)
					QMessageBox::warning(nullptr, QObject::tr("Files differ"), QObject::tr("The file\n%1\n\nis DIFFERENT from\n\n%2.").arg(filePathA, filePathB));
			});
		}
	);
}
