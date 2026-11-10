#pragma once

#include "fileoperations/usermessage.h"

#include "compiler/compiler_warnings_control.h"

#define Uses_TDrawBuffer
#define Uses_TPalette
#define Uses_TView
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
#include <QByteArray>
#include <QString>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <functional>
#include <optional>
#include <utility>
#include <vector>

[[nodiscard]] TStringView stringView(const QByteArray& utf8) noexcept;
// Replaces the middle of a text wider than `width` cells with "..."
[[nodiscard]] QString elidedInTheMiddle(const QString& text, int width);

// Lines of text in the colour of a dialog's static text. Not TStaticText: its text is fixed and capped at 255 bytes.
class CTuiTextView final : public TView
{
public:
	CTuiTextView(const TRect& bounds, QStringList lines) : TView(bounds), _lines(std::move(lines)) {}

	void setLines(QStringList lines)
	{
		_lines = std::move(lines);
		drawView();
	}

	void draw() override
	{
		TAttrPair colors = getColor(1);
		const TColorAttr color = colors[0];
		for (int row = 0; row < size.y; ++row)
		{
			TDrawBuffer buffer;
			buffer.moveChar(0, ' ', color, (ushort)size.x);
			if (row < _lines.size())
				buffer.moveStr(0, stringView(_lines[row].toUtf8()), color, (ushort)size.x);
			writeLine(0, (short)row, (short)size.x, 1, buffer);
		}
	}

	TPalette& getPalette() const override
	{
		static TPalette palette("\x06", 1); // The dialog palette's static text entry
		return palette;
	}

private:
	QStringList _lines;
};

struct DialogLine
{
	QString text;
	bool elide = false; // In the middle, for a path. Other text wraps at spaces.
};

struct DialogSpec
{
	QString title;
	std::vector<DialogLine> lines;
	QStringList buttons;
	std::optional<size_t> defaultButton; // Takes Enter while the focus is on no other button
	std::optional<size_t> focusedButton; // nullopt: the input line has the focus

	std::optional<QString> inputText; // Adds an input line holding this text
	// The button Enter in the input line presses, for the text in it; nullopt: none, Enter does nothing
	std::function<std::optional<size_t>(const QString& inputText)> enterInInput;
	QString checkBoxLabel; // Adds a checkbox unless empty
	// Called before a button closes the dialog; false keeps the dialog open
	std::function<bool(size_t button, const QString& inputText)> accepts;
};

struct DialogAnswer
{
	std::optional<size_t> button; // nullopt: closed with Esc
	QString inputText;
	bool checked = false;
};

// Shows the dialog in the middle of the desktop and returns when it closes
[[nodiscard]] DialogAnswer execDialog(const DialogSpec& spec);

void showMessage(const QString& title, const QString& text);
void showUserMessage(const UserMessage& message);
[[nodiscard]] bool confirmed(const QString& title, const QString& question, const QString& actionLabel, const QString& declineLabel = QStringLiteral("Cancel"));
// The edited text, which Enter or the action button accepts; nullopt: cancelled
[[nodiscard]] std::optional<QString> askForText(const QString& title, const DialogLine& label, const QString& text, const QString& actionLabel);

// The item chosen with Enter, Space or a double click; nullopt: closed with Esc. The cursor starts on initialItem.
[[nodiscard]] std::optional<size_t> chooseFromList(const QString& title, const QStringList& items, size_t initialItem);
