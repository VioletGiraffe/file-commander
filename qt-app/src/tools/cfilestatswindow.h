#pragma once

#include "filesystemhelpers/filestatistics.h"
#include "iconprovider/ciconprovider.h"

// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QMainWindow>
RESTORE_COMPILER_WARNINGS

#include <vector>

class CController;
class CFileListModel;
class CLabelElided;

class QLabel;
class QTimer;
class QTreeView;

// Shows the scan's progress until it finishes, then its result, or only a message when no selected path exists.
// Shows itself. Closing the window cancels the scan.
class CFileStatsWindow final : public QMainWindow, private IconsChangedListener
{
public:
	CFileStatsWindow(QWidget* parent, std::vector<QString> paths, CController& controller, CIconProvider& iconProvider);
	~CFileStatsWindow() override;

private:
	void showScanProgress();
	void showResult(const CFileStatisticsScan::Result& result);

	void onIconsChanged(const std::vector<qulonglong>& objectHashes) override;
	void onAllIconsChanged() override;

private:
	QLabel* _summaryLabel = nullptr;
	QLabel* _largestFilesLabel = nullptr;
	QTreeView* _list = nullptr;
	CFileListModel* _largestFiles = nullptr;
	CLabelElided* _statusLabel = nullptr;
	QTimer* _progressTimer = nullptr;

	QWidget* const _parentWindow;
	CIconProvider& _iconProvider;
	const QString _noPathExistsMessage;
	const QString _listedFolder; // Of the largest files: see fileListFolderPrefix()
	CFileStatisticsScan _scan;
};
