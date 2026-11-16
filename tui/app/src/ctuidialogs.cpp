#include "ctuidialogs.h"

#include "assert/advanced_assert.h"

#define Uses_TButton
#define Uses_TCheckBoxes
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TEvent
#define Uses_TFrame
#define Uses_TInputLine
#define Uses_TKeys
#define Uses_TListViewer
#define Uses_TObject
#define Uses_TProgram
#define Uses_TScrollBar
#define Uses_TSItem
#define Uses_TWindow
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <initializer_list>
#include <vector>

namespace
{

// A choice dialog's button ends the dialog with this command plus the button's index
constexpr ushort cmFirstChoice = 2000;

// Breaks a text wider than `width` cells into lines at its spaces. A longer word is elided.
// Only a text that fits keeps its indentation.
[[nodiscard]] QStringList wrappedAtSpaces(const QString& text, const int width)
{
	if (cellWidth(text) <= width)
		return { text };

	QStringList lines;
	QString line;
	for (const QString& word : text.split(u' ', Qt::SkipEmptyParts))
	{
		if (!line.isEmpty() && cellWidth(line) + 1 + cellWidth(word) > width)
		{
			lines.push_back(elidedInTheMiddle(line, width));
			line.clear();
		}
		if (!line.isEmpty())
			line += u' ';
		line += word;
	}
	lines.push_back(elidedInTheMiddle(line, width));
	return lines;
}

class CTuiChoiceDialog final : public TDialog
{
public:
	CTuiChoiceDialog(const TRect& bounds, const QString& dialogTitle) :
		TWindowInit(&TDialog::initFrame),
		TDialog(bounds, stringView(dialogTitle.toUtf8()))
	{
		options |= ofCentered;
	}

	TInputLine* inputLine = nullptr;
	std::function<std::optional<size_t>(const QString& inputText)> enterInInput;
	std::function<bool(ushort command)> accepts;
	bool ignoresEnter = false; // Outside the input line

	void handleEvent(TEvent& event) override
	{
		// Before the base class, which gives Enter to the default button, or else to the focused one
		if (event.what == evKeyDown && event.keyDown.keyCode == kbEnter)
		{
			if (enterInInput && (inputLine->state & sfFocused))
			{
				if (const std::optional<size_t> button = enterInInput(QString::fromUtf8(inputLine->data)))
					endModal((ushort)(cmFirstChoice + *button));
				clearEvent(event);
				return;
			}
			if (ignoresEnter)
			{
				clearEvent(event);
				return;
			}
		}

		TDialog::handleEvent(event);
		if (event.what == evCommand && event.message.command >= cmFirstChoice)
		{
			endModal(event.message.command);
			clearEvent(event);
		}
	}

	Boolean valid(ushort command) override
	{
		return TDialog::valid(command) && (command < cmFirstChoice || !accepts || accepts(command));
	}
};

// Makes a letter that no earlier label uses each label's hot key, except for the labels at `unmarked`
[[nodiscard]] QStringList withHotKeys(const QStringList& labels, const std::initializer_list<std::optional<size_t>> unmarked)
{
	QStringList result;
	QString usedLetters;
	for (QString label : labels)
	{
		const bool marked = std::ranges::find(unmarked, std::optional{ (size_t)result.size() }) == unmarked.end();
		for (qsizetype i = 0; marked && i < label.size(); ++i)
		{
			const QChar letter = label[i].toUpper();
			// ASCII only: Turbo Vision matches a hot key against one byte
			if (letter.unicode() >= 128 || !letter.isLetter() || usedLetters.contains(letter))
				continue;

			usedLetters += letter;
			label.insert(i + 1, u'~');
			label.insert(i, u'~');
			break;
		}
		result.push_back(std::move(label));
	}
	return result;
}

// A double click or Space on an item ends the owner dialog as OK does
class CTuiListView final : public TListViewer
{
public:
	CTuiListView(const TRect& bounds, TScrollBar* scrollBar, const QStringList& items) : TListViewer(bounds, 1, nullptr, scrollBar)
	{
		for (const QString& item : items)
			_items.push_back(item.toUtf8());
		setRange((short)_items.size());
	}

	void getText(char* dest, short item, short maxLen) override
	{
		qstrncpy(dest, _items[(size_t)item].constData(), (size_t)maxLen);
	}

	void selectItem(short item) override
	{
		TListViewer::selectItem(item);
		message(owner, evCommand, cmOK, this);
	}

private:
	std::vector<QByteArray> _items;
};

constexpr int buttonGap = 2;

// The label without its hot key marks
[[nodiscard]] int buttonWidth(const QString& label)
{
	return cellWidth(label) + 4;
}

constexpr int listDialogMargin = 3;

// The frame and the margins, plus a column for the scroll bar
[[nodiscard]] int maxListWidth()
{
	return TProgram::deskTop->size.x - 2 - 2 * listDialogMargin - 1;
}

} // namespace

TStringView stringView(const QByteArray& utf8) noexcept
{
	return { utf8.constData(), (size_t)utf8.size() };
}

void setTitle(TWindow& window, const QString& title)
{
	delete[] const_cast<char*>(window.title);
	window.title = newStr(stringView(title.toUtf8()));
	if (window.frame)
		window.frame->drawView();
}

int cellWidth(const QString& text)
{
	return strwidth(stringView(text.toUtf8()));
}

QString elidedInTheMiddle(const QString& text, const int width)
{
	if (cellWidth(text) <= width)
		return text;

	QString head = text.left(text.size() / 2), tail = text.mid(head.size());
	const auto elided = [&] { return head + QStringLiteral("...") + tail; };
	while (cellWidth(elided()) > width && !(head.isEmpty() && tail.isEmpty()))
	{
		if (head.size() >= tail.size())
			head.chop(1);
		else
			tail.remove(0, 1);
	}
	return elided();
}

QString progressBar(const std::optional<int> percent, const int width)
{
	const int barWidth = std::max(width - 5, 0);
	const int filled = percent ? barWidth * std::clamp(*percent, 0, 100) / 100 : 0;
	QString bar = QString(filled, u'█') + QString(barWidth - filled, u'░');
	if (percent)
		bar += QStringLiteral("%1%").arg(*percent, 4);
	return bar;
}

DialogAnswer execDialog(const DialogSpec& spec)
{
	static constexpr int margin = 3, inputLimit = 1024;

	const TPoint desktopSize = TProgram::deskTop->size;
	const int maxContentWidth = desktopSize.x - 2 - 2 * margin;

	int contentWidth = spec.inputText ? 60 : 34;
	for (const DialogLine& line : spec.lines)
		contentWidth = std::max(contentWidth, cellWidth(line.text));
	int allButtonsWidth = -buttonGap;
	for (const QString& label : spec.buttons)
		allButtonsWidth += buttonWidth(label) + buttonGap;
	contentWidth = std::max({ contentWidth, allButtonsWidth, cellWidth(spec.checkBoxLabel) + 5 });
	contentWidth = std::min(contentWidth, maxContentWidth);

	// Buttons fill rows from the left; a row is as wide as the contents at most
	std::vector<std::vector<size_t>> buttonRows(1);
	int rowWidth = -buttonGap;
	for (size_t i = 0; i < (size_t)spec.buttons.size(); ++i)
	{
		const int width = buttonWidth(spec.buttons[(qsizetype)i]) + buttonGap;
		if (!buttonRows.back().empty() && rowWidth + width > contentWidth)
		{
			buttonRows.emplace_back();
			rowWidth = -buttonGap;
		}
		buttonRows.back().push_back(i);
		rowWidth += width;
	}

	QStringList textLines;
	for (const DialogLine& line : spec.lines)
		textLines += line.elide ? QStringList{ elidedInTheMiddle(line.text, contentWidth) } : wrappedAtSpaces(line.text, contentWidth);

	// Rows other than the text: the frame, the margins around the text, and a blank row above each further part
	const int otherRows = 4 + (spec.inputText ? 2 : 0) + (spec.checkBoxLabel.isEmpty() ? 0 : 2) + 2 * (int)buttonRows.size();
	if (const int maxTextRows = std::max(desktopSize.y - otherRows, 1); textLines.size() > maxTextRows)
	{
		textLines.resize(maxTextRows - 1);
		textLines.push_back(QStringLiteral("..."));
	}

	const int width = contentWidth + 2 * margin;
	const int left = margin, right = margin + contentWidth;
	auto* dialog = new CTuiChoiceDialog({ 0, 0, width, (int)textLines.size() + otherRows }, spec.title);
	dialog->ignoresEnter = !spec.defaultButton;

	int y = 2;
	dialog->insert(new CTuiTextView({ left, y, right, y + (int)textLines.size() }, textLines));
	y += (int)textLines.size();

	if (spec.inputText)
	{
		++y;
		dialog->inputLine = new TInputLine({ left, y, right, y + 1 }, inputLimit);
		qstrncpy(dialog->inputLine->data, spec.inputText->toUtf8().constData(), inputLimit);
		dialog->inputLine->selectAll(True);
		dialog->insert(dialog->inputLine);
		++y;
		dialog->enterInInput = spec.enterInInput;
	}

	TCheckBoxes* checkBox = nullptr;
	if (!spec.checkBoxLabel.isEmpty())
	{
		++y;
		checkBox = new TCheckBoxes({ left, y, right, y + 1 }, new TSItem(stringView(spec.checkBoxLabel.toUtf8()), nullptr));
		dialog->insert(checkBox);
		++y;
	}

	++y;
	const QStringList buttonTitles = withHotKeys(spec.buttons, { spec.defaultButton, spec.escapeButton });
	TView* focusedView = dialog->inputLine;
	for (const std::vector<size_t>& row : buttonRows)
	{
		int x = left;
		for (const size_t i : row)
		{
			const int buttonRight = x + buttonWidth(spec.buttons[(qsizetype)i]);
			auto* button = new TButton({ x, y, buttonRight, y + 2 }, stringView(buttonTitles[(qsizetype)i].toUtf8()),
				(ushort)(cmFirstChoice + i), (ushort)(spec.defaultButton == i ? bfDefault : bfNormal));
			dialog->insert(button);
			if (spec.focusedButton == i)
				focusedView = button;
			x = buttonRight + buttonGap;
		}
		y += 2;
	}

	assert_r(focusedView);
	if (focusedView)
		focusedView->select();

	if (spec.accepts)
	{
		dialog->accepts = [&spec, dialog](const ushort command) {
			return spec.accepts(command - cmFirstChoice, dialog->inputLine ? QString::fromUtf8(dialog->inputLine->data) : QString{});
		};
	}

	const ushort command = TProgram::deskTop->execView(dialog);

	DialogAnswer answer;
	if (command >= cmFirstChoice)
		answer.button = command - cmFirstChoice;
	if (dialog->inputLine)
		answer.inputText = QString::fromUtf8(dialog->inputLine->data);
	if (checkBox)
		answer.checked = checkBox->mark(0);
	TObject::destroy(dialog);
	return answer;
}

void showMessage(const QString& title, const QString& text)
{
	DialogSpec message;
	message.title = title;
	for (const QString& line : text.split(u'\n'))
		message.lines.push_back({ line });
	message.buttons = { QStringLiteral("OK") };
	message.defaultButton = message.focusedButton = 0;
	(void)execDialog(message);
}

void showUserMessage(const UserMessage& message)
{
	showMessage(message.title, message.text);
}

bool confirmed(const QString& title, const QString& question, const QString& actionLabel, const QString& declineLabel)
{
	DialogSpec confirmation;
	confirmation.title = title;
	confirmation.lines = { { question } };
	confirmation.buttons = { actionLabel, declineLabel };
	confirmation.escapeButton = 1;
	confirmation.defaultButton = confirmation.focusedButton = 0;
	return execDialog(confirmation).button == size_t{ 0 };
}

DialogSpec textPrompt(const QString& title, const DialogLine& label, const QString& text, const QString& actionLabel)
{
	DialogSpec prompt;
	prompt.title = title;
	prompt.lines = { label };
	prompt.inputText = text;
	prompt.buttons = { actionLabel, QStringLiteral("Cancel") };
	prompt.escapeButton = 1;
	prompt.defaultButton = 0;
	prompt.enterInInput = [](const QString&) { return std::optional<size_t>{ 0 }; };
	return prompt;
}

std::optional<QString> askForText(const QString& title, const DialogLine& label, const QString& text, const QString& actionLabel)
{
	DialogAnswer answer = execDialog(textPrompt(title, label, text, actionLabel));
	if (answer.button != size_t{ 0 })
		return std::nullopt;
	return std::move(answer.inputText);
}

int chooseFromListMaxItemWidth()
{
	return maxListWidth() - 2;
}

std::optional<size_t> chooseFromList(const QString& title, const QStringList& items, const size_t initialItem, const QStringList& textAbove,
	const QString& acceptLabel, const QString& declineLabel)
{
	static constexpr int margin = listDialogMargin, minListWidth = 30;

	const TPoint desktopSize = TProgram::deskTop->size;
	// The list draws its text from its second column, and the last column keeps the text off the scroll bar
	int listWidth = minListWidth;
	for (const QString& item : items)
		listWidth = std::max(listWidth, cellWidth(item) + 2);
	// The text and the buttons also span the scroll bar's column
	for (const QString& line : textAbove)
		listWidth = std::max(listWidth, cellWidth(line) - 1);
	const int acceptWidth = buttonWidth(acceptLabel), declineWidth = buttonWidth(declineLabel);
	listWidth = std::max(listWidth, acceptWidth + buttonGap + declineWidth - 1);
	listWidth = std::min(listWidth, maxListWidth());
	const int textRows = (int)textAbove.size();
	// Rows other than the text and the list: the frame, the margins, the buttons and the blank row above them
	const int listHeight = std::clamp((int)items.size(), 1, std::max(desktopSize.y - 7 - textRows, 1));

	QStringList rows;
	for (const QString& item : items)
		rows.push_back(elidedInTheMiddle(item, listWidth - 2));

	const int width = listWidth + 2 * margin + 1;
	auto* dialog = new TDialog({ 0, 0, width, textRows + listHeight + 7 }, stringView(title.toUtf8()));
	dialog->options |= ofCentered;

	const int listRight = margin + listWidth, listTop = 2 + textRows;
	if (textRows > 0)
	{
		QStringList textLines;
		for (const QString& line : textAbove)
			textLines.push_back(elidedInTheMiddle(line, listWidth + 1));
		dialog->insert(new CTuiTextView({ margin, 2, listRight + 1, listTop }, textLines));
	}

	auto* scrollBar = new TScrollBar({ listRight, listTop, listRight + 1, listTop + listHeight });
	dialog->insert(scrollBar);
	auto* list = new CTuiListView({ margin, listTop, listRight, listTop + listHeight }, scrollBar, rows);
	dialog->insert(list);

	const int buttonsRow = listTop + listHeight + 1, declineLeft = margin + acceptWidth + buttonGap;
	// No hot keys: Enter presses the first button and Esc does what the second does
	dialog->insert(new TButton({ margin, buttonsRow, margin + acceptWidth, buttonsRow + 2 }, stringView(acceptLabel.toUtf8()), cmOK, bfDefault));
	dialog->insert(new TButton({ declineLeft, buttonsRow, declineLeft + declineWidth, buttonsRow + 2 }, stringView(declineLabel.toUtf8()), cmCancel, bfNormal));

	list->select();
	if (initialItem < (size_t)items.size())
		list->focusItem((short)initialItem);

	const ushort command = TProgram::deskTop->execView(dialog);
	const std::optional<size_t> chosen = command == cmOK && !items.isEmpty() ? std::optional<size_t>{ (size_t)list->focused } : std::nullopt;
	TObject::destroy(dialog);
	return chosen;
}
