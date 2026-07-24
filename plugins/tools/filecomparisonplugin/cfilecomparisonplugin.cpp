#include "cfilecomparisonplugin.h"
#include "plugininterface/cpluginproxy.h"

DISABLE_COMPILER_WARNINGS
#include <QApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <QMetaObject>
RESTORE_COMPILER_WARNINGS

#include <memory>

CFileCommanderPlugin* createPlugin()
{
	return new CFileComparisonPlugin;
}

CFileComparisonPlugin::CFileComparisonPlugin() noexcept :
	_progressDialog(std::make_unique<CSimpleProgressDialog>())
{
	_progressDialog->setLabelText(QObject::tr("Comparing the selected files..."));
	QObject::connect(qApp, &QCoreApplication::aboutToQuit, _progressDialog.get(), [this]() {
		_comparator.abortComparison();
		_progressDialog.reset();
	});
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

	const QString filePathA = currentItem.fullAbsolutePath();
	const QFileInfo infoA{ filePathA }, infoB{ otherFilePath };

	if (!infoA.exists())
	{
		QMessageBox::warning(nullptr, name(), QObject::tr("The file\n%1\nis selected for comparison, but doesn't exist.").arg(filePathA));
		return;
	}

	if (!infoB.exists())
	{
		QMessageBox::warning(nullptr, name(), QObject::tr("The file\n%1\nis selected for comparison, but doesn't exist.").arg(otherFilePath));
		return;
	}

	if (infoA.size() != infoB.size())
	{
		const QString msg = QObject::tr("Files have different sizes:\n%1: %2\n%3: %4").arg(filePathA).arg(infoA.size()).arg(otherFilePath).arg(infoB.size());
		QMessageBox::warning(nullptr, name(), msg);
		return;
	}

	_progressDialog->show();
	_progressDialog->adjustSize();
	auto* const progressDialog = _progressDialog.get();

	_comparator.compareFilesThreaded(filePathA, otherFilePath,
		[progressDialog](int progressPercentage) {
			QMetaObject::invokeMethod(progressDialog, [progressDialog, progressPercentage]() {
				progressDialog->setValue(progressPercentage);
			}, Qt::QueuedConnection);
		},

		[filePathA, filePathB{ otherFilePath }, progressDialog](ContentComparisonResult result) {
			QMetaObject::invokeMethod(progressDialog, [progressDialog, result, filePathA, filePathB]() {
				progressDialog->hide();

				if (result == ContentComparisonResult::Equal)
					QMessageBox::information(nullptr, QObject::tr("Files are identical"), QObject::tr("The file\n%1\n\nis IDENTICAL to\n\n%2.").arg(filePathA, filePathB));
				else if (result == ContentComparisonResult::Different)
					QMessageBox::warning(nullptr, QObject::tr("Files differ"), QObject::tr("The file\n%1\n\nis DIFFERENT from\n\n%2.").arg(filePathA, filePathB));
				else if (result == ContentComparisonResult::Error)
					QMessageBox::critical(nullptr, QObject::tr("Comparison failed"), QObject::tr("Failed to read the files:\n%1\n\n%2").arg(filePathA, filePathB));
			}, Qt::QueuedConnection);
		}
	);
}
