#pragma once

#include "filesystemhelpers/filestatistics.h"

// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QMainWindow>
RESTORE_COMPILER_WARNINGS

#include <vector>

class CController;
class CLabelElided;

class QLabel;
class QTimer;
class QTreeWidget;

// Shows the scan's progress until it finishes, then its result. Closing the window cancels the scan.
class CFileStatsWindow final : public QMainWindow
{
public:
	CFileStatsWindow(QWidget* parent, std::vector<QString> paths, CController& controller);

private:
	void showScanProgress();
	void showResult(const CFileStatisticsScan::Result& result);
	void fillFileList(const FileStatistics& stats);

private:
	QLabel* _summaryLabel = nullptr;
	QLabel* _largestFilesLabel = nullptr;
	QTreeWidget* _list = nullptr;
	CLabelElided* _statusLabel = nullptr;
	QTimer* _progressTimer = nullptr;

	CFileStatisticsScan _scan;
};
