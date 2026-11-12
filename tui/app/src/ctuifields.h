#pragma once

#include "compiler/compiler_warnings_control.h"

#define Uses_TCheckBoxes
#define Uses_TGroup
#define Uses_THistory
#define Uses_TInputLine
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
#include <QString>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <initializer_list>

// Turbo Vision's, defined in histlist.cpp and declared by no header: the size of the one block holding every field's history.
// Read when the application starts.
extern ushort historySize;

// Turbo Vision's history ids are global: every dialog's are here, so that none are shared
enum FieldHistoryId : uchar
{
	findNameHistoryId = 1,
	findContentsHistoryId,
	findSearchInHistoryId,
	batchRenameNameMaskHistoryId,
	batchRenameExtensionMaskHistoryId,
	batchRenameFindHistoryId,
	batchRenameReplaceWithHistoryId
};

[[nodiscard]] QString inputText(const TInputLine* input);
// Selects all of it, so typing replaces it
void setInputText(TInputLine* input, const QString& text);

// The items' states, in the items' order
void setChecked(TCheckBoxes* checkBoxes, std::initializer_list<bool> checked);
[[nodiscard]] bool isChecked(TCheckBoxes* checkBoxes, int item);

// A field's earlier texts, most recent first, under the settings key of the GUI's field: the two front ends share them
class CTuiSavedFieldHistory
{
public:
	// Loads the texts, and the newest that fit into Turbo Vision's history
	CTuiSavedFieldHistory(QString settingsKey, FieldHistoryId historyId);

	[[nodiscard]] QString mostRecent() const { return _entries.value(0); }
	// Moves the text to the front and saves the history, at most FIELD_HISTORY_MAX_ENTRIES of it. Empty text is not recorded.
	void record(const QString& text);
	// The button beside `input` that opens the list. It never records the field's text: the dialog calls record().
	[[nodiscard]] THistory* newButton(const TRect& bounds, TInputLine* input) const;

private:
	QStringList _entries;
	const QString _settingsKey;
	const FieldHistoryId _historyId;
};

// A labelled input line from fieldX to fieldEnd, the last 3 columns holding the history's button.
// stretches: the input line widens with the owner, and the button keeps its distance from the owner's right edge.
TInputLine* insertFieldWithHistory(TGroup& owner, const CTuiSavedFieldHistory& history, const char* label, int y, int labelX, int fieldX, int fieldEnd, int maxBytes, bool stretches);
