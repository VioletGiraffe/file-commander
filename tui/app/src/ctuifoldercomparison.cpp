#include "ctuifoldercomparison.h"
#include "ctuidialogs.h"
#include "ctuirowlist.h"

#include "ccontroller.h"
#include "filesystemhelperfunctions.h"

#include "fileoperations/centrypath.h"

#include "assert/advanced_assert.h"

#define Uses_TButton
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TObject
#define Uses_TPalette
#define Uses_TProgram
#define Uses_TScrollBar
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
#include <QLocale>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <utility>
#include <vector>

namespace {

constexpr ushort cmGoToDifference = 2300, cmRefreshComparison = 2301;

// The comparison with a result; nullptr when cancelled
[[nodiscard]] std::unique_ptr<CFolderComparison> comparedFolders(QString leftRoot, QString rightRoot)
{
	auto comparison = std::make_unique<CFolderComparison>(std::move(leftRoot), std::move(rightRoot));
	const bool finished = waitWithProgressDialog(QStringLiteral("Comparing folders"), [&comparison] { return comparison->finished(); }, [&comparison](const int width) {
		const std::optional<int> percent = comparison->percent();
		return QStringList{ percent ? QStringLiteral("Comparing file contents...") : QStringLiteral("Scanning both folders..."), percent ? progressBar(percent, width) : QString{} };
	});

	// The destructor cancels an unfinished comparison
	if (!finished || !comparison->result())
		return nullptr;
	return comparison;
}

[[nodiscard]] bool isSameOrBelow(const QString& folder, const QString& root)
{
	const std::optional<CEntryPath> folderPath = parseOperationPath(folder), rootPath = parseOperationPath(root);
	return folderPath && rootPath && (*folderPath == *rootPath || folderPath->isStrictDescendantOf(*rootPath));
}

class CTuiDifferencesList final : public CTuiRowList
{
public:
	using CTuiRowList::CTuiRowList;

	void setRows(QStringList rows, const int cursorRow)
	{
		_rows = std::move(rows);
		rowsChanged(cursorRow);
	}

	void handleEvent(TEvent& event) override
	{
		CTuiRowList::handleEvent(event);
		clearPlainTextKey(event);
	}

	void draw() override
	{
		const TColorAttr normalColor = mapColor(1), cursorColor = mapColor(2);
		for (int y = 0; y < size.y; ++y)
		{
			const int rowIndex = topRow() + y;
			const TColorAttr color = cursorIsShownOn(rowIndex) ? cursorColor : normalColor;

			TDrawBuffer line;
			line.moveChar(0, ' ', color, (ushort)size.x);
			if (rowIndex < (int)_rows.size())
				line.moveStr(1, stringView(_rows[rowIndex].toUtf8()), color, (ushort)std::max(size.x - 2, 0));
			writeLine(0, (short)y, (short)size.x, 1, line);
		}
	}

	TPalette& getPalette() const override
	{
		static TPalette palette("\x1A\x1B", 2); // The dialog palette's list viewer entries: normal, focused
		return palette;
	}

private:
	[[nodiscard]] int rowCount() const noexcept override { return (int)_rows.size(); }

private:
	QStringList _rows;
};

// Ends with cmGoToDifference on a difference, cmRefreshComparison, or cmCancel
class CTuiDifferencesDialog final : public TDialog
{
public:
	CTuiDifferencesDialog(const TRect& bounds, const CFolderComparison& comparison, const int cursorRow) :
		TWindowInit(&TDialog::initFrame),
		TDialog(bounds, "Folder comparison")
	{
		options |= ofCentered;

		static constexpr int margin = 3, buttonWidth = 12, buttonGap = 2;
		const int right = size.x - margin, buttonsTop = size.y - 3, textWidth = right - margin;

		const FolderComparisonResult& result = *comparison.result();
		insert(new CTuiTextView({ margin, 2, right, 5 }, {
			elidedInTheMiddle(QStringLiteral("Left: ") + toNativeSeparators(comparison.leftRoot()), textWidth),
			elidedInTheMiddle(QStringLiteral("Right: ") + toNativeSeparators(comparison.rightRoot()), textWidth),
			folderComparisonSummary(result),
		}));

		const QLocale locale;
		const auto sizeText = [&locale](const std::optional<uint64_t> fileSize) { return fileSize ? locale.formattedDataSize((qint64)*fileSize) : QString{}; };
		const QString statusTitle = QStringLiteral("Status"), leftTitle = QStringLiteral("Left"), rightTitle = QStringLiteral("Right");
		qsizetype statusWidth = statusTitle.size(), sizeWidth = std::max(leftTitle.size(), rightTitle.size());
		for (const FolderComparisonEntry& entry : result.differences)
		{
			statusWidth = std::max(statusWidth, folderComparisonStatusText(entry.status).size());
			sizeWidth = std::max({ sizeWidth, sizeText(entry.leftSize).size(), sizeText(entry.rightSize).size() });
		}

		// The path comes last: it is the only column elided, and it may hold characters wider than a cell.
		// The list draws from its second column and keeps its last one clear, beside the scroll bar.
		const int listWidth = right - 1 - margin;
		const int pathWidth = std::max(listWidth - 2 - (int)(statusWidth + 2 * sizeWidth) - 6, 1);
		const auto rowText = [&](const QString& status, const QString& leftSize, const QString& rightSize, const QString& path) {
			return status.leftJustified(statusWidth) + QStringLiteral("  ") + leftSize.rightJustified(sizeWidth) + QStringLiteral("  ") +
				rightSize.rightJustified(sizeWidth) + QStringLiteral("  ") + elidedInTheMiddle(path, pathWidth);
		};

		insert(new CTuiTextView({ margin + 1, 6, right - 1, 7 }, { rowText(statusTitle, leftTitle, rightTitle, QStringLiteral("Name")) }));

		QStringList rows;
		rows.reserve((qsizetype)result.differences.size());
		for (const FolderComparisonEntry& entry : result.differences)
			rows.push_back(rowText(folderComparisonStatusText(entry.status), sizeText(entry.leftSize), sizeText(entry.rightSize), toNativeSeparators(entry.relativePath)));
		_hasRows = !rows.isEmpty();

		auto* scrollBar = new TScrollBar({ right - 1, 7, right, buttonsTop - 1 });
		insert(scrollBar);
		_list = new CTuiDifferencesList({ margin, 7, right - 1, buttonsTop - 1 }, scrollBar);
		insert(_list);
		_list->setRows(std::move(rows), cursorRow);

		// Enter presses Go to, and Esc does what Close does: neither has a hot key
		const std::pair<const char*, ushort> buttons[]{ { "Go to", cmGoToDifference }, { "~R~efresh", cmRefreshComparison }, { "Close", cmCancel } };
		int x = margin;
		for (const auto& [buttonTitle, command] : buttons)
		{
			insert(new CTuiButton({ x, buttonsTop, x + buttonWidth, buttonsTop + 2 }, buttonTitle, command, command == cmGoToDifference ? bfDefault : bfNormal));
			x += buttonWidth + buttonGap;
		}

		_list->select();
	}

	[[nodiscard]] int cursorRow() const noexcept { return _list->cursorRow(); }

	void handleEvent(TEvent& event) override
	{
		TDialog::handleEvent(event);
		if (event.what != evCommand)
			return;

		switch (event.message.command)
		{
		case cmGoToDifference:
		case cmActivateRowUnderCursor:
			if (_hasRows)
				endModal(cmGoToDifference);
			clearEvent(event);
			break;
		case cmRefreshComparison:
			endModal(cmRefreshComparison);
			clearEvent(event);
			break;
		default:
			break;
		}
	}

private:
	CTuiDifferencesList* _list; // Owned by this dialog
	bool _hasRows = false;
};

} // namespace

std::optional<CFolderComparison::EntryLocation> CTuiFolderComparison::run()
{
	const QString leftFolder = _controller.panel(Panel::LeftPanel).currentDirPathPosix();
	const QString rightFolder = _controller.panel(Panel::RightPanel).currentDirPathPosix();
	if (!_lastComparison || !isSameOrBelow(leftFolder, _lastComparison->leftRoot()) || !isSameOrBelow(rightFolder, _lastComparison->rightRoot()))
	{
		std::unique_ptr<CFolderComparison> comparison = comparedFolders(leftFolder, rightFolder);
		if (!comparison)
			return std::nullopt;

		_lastComparison = std::move(comparison);
		_cursorRow = 0;
	}

	for (;;)
	{
		const std::vector<FolderComparisonEntry>& differences = _lastComparison->result()->differences;
		const TPoint desktopSize = TProgram::deskTop->size;
		// The list's rows plus 11: the frame, the text above the list, and the buttons
		const int listRows = (int)std::min<size_t>(std::max<size_t>(differences.size(), 1), (size_t)desktopSize.y);
		const int height = std::min(listRows + 11, (int)desktopSize.y);
		auto* dialog = new CTuiDifferencesDialog({ 0, 0, std::min<int>(desktopSize.x, 110), height }, *_lastComparison, _cursorRow);
		const ushort command = TProgram::deskTop->execView(dialog);
		_cursorRow = dialog->cursorRow();
		TObject::destroy(dialog);

		if (command == cmGoToDifference)
		{
			assert_and_return_r(_cursorRow >= 0 && (size_t)_cursorRow < differences.size(), std::nullopt);
			return _lastComparison->locationOf(differences[(size_t)_cursorRow]);
		}
		if (command != cmRefreshComparison)
			return std::nullopt;

		// A cancelled refresh lists the last result again
		if (std::unique_ptr<CFolderComparison> comparison = comparedFolders(_lastComparison->leftRoot(), _lastComparison->rightRoot()))
			_lastComparison = std::move(comparison);
	}
}
