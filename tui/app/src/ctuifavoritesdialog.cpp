#include "ctuifavoritesdialog.h"
#include "ctuidialogs.h"
#include "ctuirowlist.h"

#include "cfilesystemobject.h"

#include "assert/advanced_assert.h"

#define Uses_TButton
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TKeys
#define Uses_TPalette
#define Uses_TProgram
#define Uses_TScrollBar
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <utility>
#include <vector>

namespace {

constexpr ushort cmGoToFavorite = 2200, cmGoUpOneLevel = 2201, cmAddCurrentFolder = 2202, cmAddCategory = 2203, cmEditFavorite = 2204, cmRemoveFavorite = 2205;

struct FavoriteRow
{
	QString name; // ".." for the row leading to the parent category
	QString path; // Empty for a category
	bool isCategory = false;
};

// A category's items, after the row leading to its parent. A category's name starts with a mark, a location's path follows its name.
class CTuiFavoritesList final : public CTuiRowList
{
public:
	using CTuiRowList::CTuiRowList;

	void setRows(std::vector<FavoriteRow> rows, const int cursorRow)
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
		static constexpr int markWidth = 2;

		int nameWidth = 0;
		for (const FavoriteRow& row : _rows)
			nameWidth = std::max(nameWidth, cellWidth(row.name));
		nameWidth = std::clamp(nameWidth, 1, std::max((size.x - 1) / 2 - markWidth, 1));

		for (int y = 0; y < size.y; ++y)
		{
			const int rowIndex = topRow() + y;
			const TColorAttr color = cursorIsShownOn(rowIndex) ? cursorColor : normalColor;

			TDrawBuffer line;
			line.moveChar(0, ' ', color, (ushort)size.x);
			if (rowIndex < (int)_rows.size())
			{
				const FavoriteRow& row = _rows[(size_t)rowIndex];
				QString text = (row.isCategory ? QStringLiteral("▸ ") : QStringLiteral("  ")) + elidedInTheMiddle(row.name, nameWidth);
				if (!row.path.isEmpty())
				{
					text += QString(markWidth + nameWidth - cellWidth(text), u' ') + QStringLiteral("  ");
					text += elidedInTheMiddle(row.path, size.x - 2 - cellWidth(text));
				}
				line.moveStr(1, stringView(text.toUtf8()), color, (ushort)std::max(size.x - 2, 0));
			}
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
	std::vector<FavoriteRow> _rows;
};

class CTuiFavoritesDialog final : public TDialog
{
public:
	CTuiFavoritesDialog(const TRect& bounds, CFavoriteLocations& favorites, QString currentFolder) :
		TWindowInit(&TDialog::initFrame),
		TDialog(bounds, "Favorites"),
		_favorites(favorites),
		_currentFolder(std::move(currentFolder))
	{
		options |= ofCentered;

		static constexpr int margin = 3, buttonWidth = 16, buttonGap = 2;
		const int right = size.x - margin, buttonsTop = size.y - 6;

		_levelText = new CTuiTextView({ margin, 2, right, 3 }, {});
		insert(_levelText);

		auto* scrollBar = new TScrollBar({ right - 1, 4, right, buttonsTop - 1 });
		insert(scrollBar);
		_list = new CTuiFavoritesList({ margin, 4, right - 1, buttonsTop - 1 }, scrollBar);
		insert(_list);

		// Enter presses Go to, and Esc does what Close does: neither has a hot key
		const std::pair<const char*, ushort> buttons[2][3]{
			{ { "Go to", cmGoToFavorite }, { "~A~dd folder", cmAddCurrentFolder }, { "~N~ew category", cmAddCategory } },
			{ { "~R~ename", cmEditFavorite }, { "Re~m~ove", cmRemoveFavorite }, { "Close", cmCancel } },
		};
		for (int row = 0; row < 2; ++row)
		{
			for (int column = 0; column < 3; ++column)
			{
				const int x = margin + column * (buttonWidth + buttonGap), y = buttonsTop + 2 * row;
				const auto& [buttonTitle, command] = buttons[row][column];
				insert(new TButton({ x, y, x + buttonWidth, y + 2 }, buttonTitle, command, command == cmGoToFavorite ? bfDefault : bfNormal));
			}
		}

		_list->select();
		showLevel(std::nullopt);
	}

	// The location to go to; nullopt when closed without one
	[[nodiscard]] const std::optional<QString>& chosenLocation() const noexcept { return _chosenLocation; }

	void handleEvent(TEvent& event) override
	{
		// Before the base class, which gives Enter to the default button
		if (event.what == evKeyDown)
		{
			if (const ushort command = commandForKey(event.keyDown.keyCode); command && runCommand(command))
			{
				clearEvent(event);
				return;
			}
		}

		TDialog::handleEvent(event);
		if (event.what == evCommand && runCommand(event.message.command))
			clearEvent(event);
	}

private:
	// Keys as in the panel
	[[nodiscard]] ushort commandForKey(const ushort keyCode) const
	{
		switch (keyCode)
		{
		case kbBack: return cmGoUpOneLevel;
		case kbIns: return cmAddCurrentFolder;
		case kbF7: return cmAddCategory;
		case kbF2: return cmEditFavorite;
		case kbDel: case kbF8: return cmRemoveFavorite;
		default: return 0;
		}
	}

	// False for a command the dialog does not carry out
	bool runCommand(const ushort command)
	{
		switch (command)
		{
		case cmGoToFavorite:
		case cmActivateRowUnderCursor:
			activateCurrentRow();
			break;
		case cmGoUpOneLevel: goUpOneLevel(); break;
		case cmAddCurrentFolder: addCurrentFolder(); break;
		case cmAddCategory: addCategory(); break;
		case cmEditFavorite: editCurrentItem(); break;
		case cmRemoveFavorite: removeCurrentItem(); break;
		default: return false;
		}
		return true;
	}

	// The category shown, found from the top again on every use: a change to a list moves its items
	[[nodiscard]] std::vector<CLocationsCollection>& shownList()
	{
		std::vector<CLocationsCollection>* list = &_favorites.locations();
		for (const size_t index : _categoryPath)
		{
			assert_and_return_r(index < list->size(), *list);
			list = &(*list)[index].subLocations;
		}
		return *list;
	}

	// The row leading to the parent category comes first below the top level
	[[nodiscard]] int firstItemRow() const noexcept { return _categoryPath.empty() ? 0 : 1; }

	// Of the shown category's list; nullopt on the row leading to the parent category, or in an empty list
	[[nodiscard]] std::optional<size_t> currentItemIndex()
	{
		const int index = _list->cursorRow() - firstItemRow();
		if (index < 0 || index >= (int)shownList().size())
			return std::nullopt;
		return (size_t)index;
	}

	// cursorItem: an index in the shown category's list; nullopt puts the cursor on its first item
	void showLevel(const std::optional<size_t> cursorItem)
	{
		QString levelText = QStringLiteral("Favorites");
		const std::vector<CLocationsCollection>* list = &_favorites.locations();
		for (const size_t index : _categoryPath)
		{
			levelText += QStringLiteral(" ▸ ") + (*list)[index].displayName;
			list = &(*list)[index].subLocations;
		}
		_levelText->setLines({ elidedInTheMiddle(levelText, _levelText->size.x) });

		std::vector<FavoriteRow> rows;
		if (!_categoryPath.empty())
			rows.push_back({ QStringLiteral(".."), {}, false });
		for (const CLocationsCollection& item : *list)
			rows.push_back({ item.displayName, item.absolutePath, item.isCategory() });

		_list->setRows(std::move(rows), list->empty() ? 0 : firstItemRow() + (int)cursorItem.value_or(0));
	}

	void activateCurrentRow()
	{
		const std::optional<size_t> index = currentItemIndex();
		if (!index)
		{
			goUpOneLevel();
			return;
		}

		const CLocationsCollection& item = shownList()[*index];
		if (item.isCategory())
		{
			_categoryPath.push_back(*index);
			showLevel(std::nullopt);
		}
		else
		{
			_chosenLocation = item.absolutePath;
			endModal(cmOK);
		}
	}

	// The cursor goes to the category left
	void goUpOneLevel()
	{
		if (_categoryPath.empty())
			return;

		const size_t leftCategory = _categoryPath.back();
		_categoryPath.pop_back();
		showLevel(leftCategory);
	}

	// Shows the refusal, or the shown category with the cursor on cursorItem
	void showResult(const std::optional<UserMessage>& refusal, const std::optional<size_t> cursorItem)
	{
		if (refusal)
			showUserMessage(*refusal);
		else
			showLevel(cursorItem);
	}

	void addCurrentFolder()
	{
		const QString folderName = CFileSystemObject{ _currentFolder }.name();
		const std::optional<QString> favoriteName = askForText(QStringLiteral("Add to favorites"), { QStringLiteral("Name for %1:").arg(_currentFolder), true },
			folderName.isEmpty() ? _currentFolder : folderName, QStringLiteral("Add"));
		if (!favoriteName)
			return;

		std::vector<CLocationsCollection>& list = shownList();
		showResult(_favorites.addLocation(list, *favoriteName, _currentFolder), list.size() - 1);
	}

	void addCategory()
	{
		const std::optional<QString> categoryName = askForText(QStringLiteral("New category"), { QStringLiteral("Name of the new category:") }, {}, QStringLiteral("Create"));
		if (!categoryName)
			return;

		std::vector<CLocationsCollection>& list = shownList();
		showResult(_favorites.addCategory(list, *categoryName), list.size() - 1);
	}

	// Asks for the name, and for a location its path too
	void editCurrentItem()
	{
		const std::optional<size_t> index = currentItemIndex();
		if (!index)
			return;

		const CLocationsCollection& item = shownList()[*index];
		const bool isCategory = item.isCategory();
		const std::optional<QString> newName = askForText(QStringLiteral("Rename"), { QStringLiteral("Name:") }, item.displayName,
			isCategory ? QStringLiteral("Rename") : QStringLiteral("Next"));
		if (!newName)
			return;

		std::optional<QString> path;
		if (!isCategory)
		{
			path = askForText(QStringLiteral("Rename"), { QStringLiteral("Location:") }, shownList()[*index].absolutePath, QStringLiteral("Save"));
			if (!path)
				return;
		}

		showResult(_favorites.editItem(shownList(), *index, *newName, path.value_or(QString{})), index);
	}

	void removeCurrentItem()
	{
		const std::optional<size_t> index = currentItemIndex();
		if (!index)
			return;

		const CLocationsCollection& item = shownList()[*index];
		const QString question = item.isCategory() ? QStringLiteral("Remove the category \"%1\" and everything in it?").arg(item.displayName)
			: QStringLiteral("Remove \"%1\" from the favorites?").arg(item.displayName);
		if (!confirmed(QStringLiteral("Remove"), question, QStringLiteral("Remove")))
			return;

		std::vector<CLocationsCollection>& list = shownList();
		_favorites.removeItem(list, *index);
		showLevel(list.empty() ? std::nullopt : std::optional<size_t>{ std::min(*index, list.size() - 1) });
	}

private:
	CFavoriteLocations& _favorites;
	const QString _currentFolder;
	// The index of each category on the way to the one shown, from the top level
	std::vector<size_t> _categoryPath;
	std::optional<QString> _chosenLocation;

	// Owned by this dialog
	CTuiTextView* _levelText;
	CTuiFavoritesList* _list;
};

} // namespace

std::optional<QString> runFavoritesDialog(CFavoriteLocations& favorites, const QString& currentFolder)
{
	const TPoint desktopSize = TProgram::deskTop->size;
	auto* dialog = new CTuiFavoritesDialog({ 0, 0, std::min<int>(desktopSize.x, 90), std::min<int>(desktopSize.y, 24) }, favorites, currentFolder);
	TProgram::deskTop->execView(dialog);
	std::optional<QString> chosenLocation = dialog->chosenLocation();
	TObject::destroy(dialog);
	return chosenLocation;
}
