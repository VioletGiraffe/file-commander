#include "ctuifields.h"
#include "ctuidialogs.h"

#include "settings.h"

#define Uses_TLabel
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
#include <QByteArray>
#include <QSettings>
RESTORE_COMPILER_WARNINGS

#include <utility>
#include <vector>

namespace {

// Turbo Vision keeps the histories of all fields in one block, historySize bytes, and drops the oldest entries when it is full
constexpr qsizetype maxHistoryEntriesShown = 8;
// Turbo Vision stores an entry's length plus 3 in one byte: a longer entry corrupts the history
constexpr qsizetype maxHistoryEntryBytes = 252;

// Leaves out an entry too long for Turbo Vision's history
void addToTurboVisionHistory(uchar historyId, const QByteArray& utf8)
{
	if (utf8.size() <= maxHistoryEntryBytes)
		historyAdd(historyId, stringView(utf8));
}

// Never records its field's text, which THistory does when its list opens and on any button press
class CTuiFieldHistory final : public THistory
{
public:
	using THistory::THistory;

	void recordHistory(const char* /*text*/) override {}
};

} // namespace

QString inputText(const TInputLine* input)
{
	return QString::fromUtf8(input->data);
}

void setInputText(TInputLine* input, const QString& text)
{
	strnzcpy(input->data, stringView(text.toUtf8()), (size_t)input->maxLen + 1);
	input->selectAll(True);
}

void setChecked(TCheckBoxes* checkBoxes, std::initializer_list<bool> checked)
{
	ushort bits = 0;
	for (int item = 0; const bool itemChecked : checked)
		bits |= (ushort)(itemChecked << item++);
	checkBoxes->setData(&bits);
}

bool isChecked(TCheckBoxes* checkBoxes, int item)
{
	return checkBoxes->mark(item) != False;
}

CTuiSavedFieldHistory::CTuiSavedFieldHistory(QString settingsKey, const FieldHistoryId historyId) :
	_entries(QSettings{}.value(settingsKey).toStringList()),
	_settingsKey(std::move(settingsKey)),
	_historyId(historyId)
{
	// The newest entries that fit, added oldest first: Turbo Vision keeps a history's most recent entry last
	std::vector<QByteArray> newestFirst;
	for (qsizetype i = 0; i < _entries.size() && (qsizetype)newestFirst.size() < maxHistoryEntriesShown; ++i)
	{
		if (QByteArray utf8 = _entries[i].toUtf8(); utf8.size() <= maxHistoryEntryBytes)
			newestFirst.push_back(std::move(utf8));
	}

	for (auto entry = newestFirst.rbegin(); entry != newestFirst.rend(); ++entry)
		addToTurboVisionHistory(_historyId, *entry);
}

void CTuiSavedFieldHistory::record(const QString& text)
{
	if (text.isEmpty())
		return;

	_entries.removeAll(text);
	_entries.push_front(text);
	if (_entries.size() > FIELD_HISTORY_MAX_ENTRIES)
		_entries.resize(FIELD_HISTORY_MAX_ENTRIES);
	QSettings{}.setValue(_settingsKey, _entries);
	addToTurboVisionHistory(_historyId, text.toUtf8());
}

THistory* CTuiSavedFieldHistory::newButton(const TRect& bounds, TInputLine* input) const
{
	return new CTuiFieldHistory(bounds, input, _historyId);
}

TInputLine* insertFieldWithHistory(TGroup& owner, const CTuiSavedFieldHistory& history, const char* label, const int y, const int labelX, const int fieldX, const int fieldEnd,
	const int maxBytes, const bool stretches)
{
	const int historyX = fieldEnd - 3;
	auto* input = new TInputLine({ fieldX, y, historyX, y + 1 }, maxBytes);
	owner.insert(input);
	owner.insert(new TLabel({ labelX, y, fieldX - 1, y + 1 }, label, input));
	THistory* button = history.newButton({ historyX, y, fieldEnd, y + 1 }, input);
	owner.insert(button);
	if (stretches)
	{
		input->growMode = gfGrowHiX;
		button->growMode = gfGrowLoX | gfGrowHiX;
	}
	return input;
}
