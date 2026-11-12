#include "ctuibatchrenamedialog.h"
#include "ctuidialogs.h"
#include "ctuifields.h"
#include "ctuirowlist.h"

#include "batchrename/cbatchrenamesession.h"
#include "settings.h"

#define Uses_TButton
#define Uses_TCheckBoxes
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TDrawBuffer
#define Uses_TEvent
#define Uses_TEventQueue
#define Uses_TFilterValidator
#define Uses_TInputLine
#define Uses_TKeys
#define Uses_TLabel
#define Uses_TPalette
#define Uses_TProgram
#define Uses_TRadioButtons
#define Uses_TScreen
#define Uses_TScrollBar
#define Uses_TSItem
DISABLE_COMPILER_WARNINGS
#include <tvision/tv.h>
#include <QString>
#include <QStringList>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <chrono>
#include <initializer_list>
#include <utility>

using namespace std::chrono_literals;
using Step = CBatchRenameSession::Step;

namespace {

// Disabled while they cannot run: Turbo Vision disables only the commands up to 255
constexpr ushort cmRename = 200, cmUndo = 201;
// The check boxes' items, in their order
enum FindOption { FindIsRegex, FindIsCaseSensitive };
constexpr int maskInputLimit = 1024; // Bytes
constexpr auto escapeCheckInterval = 50ms;

// The preview's columns, a third of its width each: the name, the new name, the status
struct PreviewColumns
{
	int newNameX, statusX;
};

[[nodiscard]] PreviewColumns previewColumns(const int width)
{
	return { width / 3, 2 * width / 3 };
}

// Takes the keys typed meanwhile, which a renaming drops: true when one of them is Esc
[[nodiscard]] bool escapeTyped()
{
	bool escape = false;
	for (;;)
	{
		TEvent event;
		TEventQueue::getKeyEvent(event);
		if (event.what == evNothing)
			return escape;
		escape |= event.what == evKeyDown && event.keyDown.keyCode == kbEsc;
	}
}

// The session's items: the name, the new name and the status, which starts with a mark for an error
class CTuiBatchRenamePreview final : public CTuiRowList
{
public:
	CTuiBatchRenamePreview(const TRect& bounds, TScrollBar* verticalScrollBar, const CBatchRenameSession& session) noexcept :
		CTuiRowList(bounds, verticalScrollBar),
		_session(session)
	{}

	// After the session's rows change
	void showSession() { rowsChanged(cursorRow()); }

	void handleEvent(TEvent& event) override
	{
		CTuiRowList::handleEvent(event);
		clearPlainTextKey(event);
	}

	void draw() override
	{
		const TColorAttr normalColor = mapColor(1), cursorColor = mapColor(2);
		const PreviewColumns columns = previewColumns(size.x);
		const std::vector<CBatchRenameSession::Row>& rows = _session.rows();
		for (int y = 0; y < size.y; ++y)
		{
			const int row = topRow() + y;
			const TColorAttr color = cursorIsShownOn(row) ? cursorColor : normalColor;

			TDrawBuffer line;
			line.moveChar(0, ' ', color, (ushort)size.x);
			if (row < (int)rows.size())
			{
				const CBatchRenameSession::Row& item = rows[(size_t)row];
				const CBatchRenameSession::Status status = CBatchRenameSession::statusOf(item);
				const auto moveText = [&line, color](const int x, const int width, const QString& text) {
					line.moveStr((ushort)x, stringView(elidedInTheMiddle(text, width).toUtf8()), color, (ushort)std::max(width, 0));
				};
				moveText(0, columns.newNameX - 1, item.path.name());
				moveText(columns.newNameX, columns.statusX - columns.newNameX - 1, item.newName);
				moveText(columns.statusX, size.x - columns.statusX, status.tone == CBatchRenameSession::Tone::Error ? QStringLiteral("✗ ") + status.text : status.text);
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
	[[nodiscard]] int rowCount() const noexcept override { return (int)_session.rows().size(); }

private:
	const CBatchRenameSession& _session;
};

// The GUI's batch rename dialog: the rules above, the session's preview below.
// Sized to the desktop when it opens; it does not follow the terminal's size.
class CTuiBatchRenameDialog final : public TDialog
{
public:
	CTuiBatchRenameDialog(const TRect& bounds, const std::vector<CFileSystemObject>& items);

	// Turbo Vision reports no edit: every event checks the fields for a change of the rules
	void handleEvent(TEvent& event) override;

	[[nodiscard]] BatchRenameRules rules() const;

private:
	void showSession();
	// The fields' texts go to their histories when the rules are applied
	void runStep(Step step);
	void setStatus(const QString& text);

private:
	CTuiSavedFieldHistory _nameMaskHistory, _extensionMaskHistory, _findHistory, _replaceWithHistory;
	CBatchRenameSession _session;
	BatchRenameRules _sessionRules; // The rules the session has

	// Owned by this dialog
	TInputLine* _nameMask;
	TInputLine* _extensionMask;
	TInputLine* _find;
	TInputLine* _replaceWith;
	TCheckBoxes* _findOptions;
	TRadioButtons* _letterCase; // In LetterCase order
	TInputLine* _counterStart;
	TInputLine* _counterStep;
	TInputLine* _counterDigits;
	CTuiBatchRenamePreview* _preview;
	CTuiTextView* _status;
};

CTuiBatchRenameDialog::CTuiBatchRenameDialog(const TRect& bounds, const std::vector<CFileSystemObject>& items) :
	TWindowInit(&TDialog::initFrame),
	TDialog(bounds, "Batch rename"),
	_nameMaskHistory(KEY_BATCH_RENAME_NAME_MASK_HISTORY, batchRenameNameMaskHistoryId),
	_extensionMaskHistory(KEY_BATCH_RENAME_EXTENSION_MASK_HISTORY, batchRenameExtensionMaskHistoryId),
	_findHistory(KEY_BATCH_RENAME_FIND_HISTORY, batchRenameFindHistoryId),
	_replaceWithHistory(KEY_BATCH_RENAME_REPLACE_WITH_HISTORY, batchRenameReplaceWithHistoryId),
	_session(items),
	_sessionRules(CBatchRenameSession::initialRules())
{
	flags &= ~wfMove;

	static constexpr int labelX = 2, fieldX = 20, listTop = 13, renameButtonWidth = 12, otherButtonWidth = 10;
	const int right = size.x - 2, buttonY = size.y - 3, statusY = size.y - 2;

	_nameMask = insertFieldWithHistory(*this, _nameMaskHistory, "~N~ame mask:", 2, labelX, fieldX, right, maskInputLimit, false);
	_extensionMask = insertFieldWithHistory(*this, _extensionMaskHistory, "~E~xtension mask:", 3, labelX, fieldX, right, maskInputLimit, false);
	insert(new CTuiTextView({ fieldX, 4, right, 6 }, { QStringLiteral("[N] name, [N2-5] its characters 2 to 5, [E] extension, [P] parent folder,"),
		QStringLiteral("[C] counter, [Y] [M] [D] [h] [m] [s] modification time") }));

	// The "with" label starts in the middle, its field ends at the right
	const int withX = fieldX + (right - fieldX) / 2;
	_find = insertFieldWithHistory(*this, _findHistory, "Re~p~lace:", 6, labelX, fieldX, withX - 1, maskInputLimit, false);
	_replaceWith = insertFieldWithHistory(*this, _replaceWithHistory, "~w~ith", 6, withX, withX + 6, right, maskInputLimit, false);
	_findOptions = new TCheckBoxes({ fieldX, 7, right, 8 }, new TSItem("Re~g~ular expression", new TSItem("Case-~s~ensitive", nullptr)));
	insert(_findOptions);

	_letterCase = new TRadioButtons({ fieldX, 8, fieldX + 34, 10 },
		new TSItem("Unchanged", new TSItem("lowercase", new TSItem("UPPERCASE", new TSItem("Title Case", nullptr)))));
	insert(_letterCase);
	insert(new TLabel({ labelX, 8, fieldX - 1, 9 }, "~L~etter case:", _letterCase));

	// TLabel draws its text from its second column
	insert(new CTuiTextView({ labelX + 1, 10, fieldX - 1, 11 }, { QStringLiteral("Counter [C]:") }));
	// A labelled number field from x, the label taking labelWidth columns
	const auto addNumberField = [this](const int x, const int labelWidth, const char* label, const CBatchRenameSession::Range range) {
		const int digits = (int)QString::number(range.max).size();
		auto* input = new TInputLine({ x + labelWidth, 10, x + labelWidth + digits + 2, 11 }, digits, new TFilterValidator("0123456789"));
		insert(input);
		insert(new TLabel({ x, 10, x + labelWidth, 11 }, label, input));
		return input;
	};
	_counterStart = addNumberField(fieldX, 7, "St~a~rt:", CBatchRenameSession::counterStartRange);
	_counterStep = addNumberField(fieldX + 19, 6, "S~t~ep:", CBatchRenameSession::counterStepRange);
	_counterDigits = addNumberField(fieldX + 35, 8, "~D~igits:", CBatchRenameSession::counterDigitsRange);

	const PreviewColumns columns = previewColumns(right - 1 - labelX);
	insert(new CTuiTextView({ labelX, listTop - 1, right - 1, listTop },
		{ QStringLiteral("Name").leftJustified(columns.newNameX) + QStringLiteral("New name").leftJustified(columns.statusX - columns.newNameX) + QStringLiteral("Status") }));
	auto* scrollBar = new TScrollBar({ right - 1, listTop, right, buttonY });
	insert(scrollBar);
	_preview = new CTuiBatchRenamePreview({ labelX, listTop, right - 1, buttonY }, scrollBar, _session);
	insert(_preview);

	const int closeX = right - otherButtonWidth, undoX = closeX - otherButtonWidth, renameX = undoX - renameButtonWidth;
	// No default button: Enter in a field must not rename
	insert(new TButton({ renameX, buttonY, undoX, buttonY + 2 }, "~R~ename", cmRename, bfNormal));
	insert(new TButton({ undoX, buttonY, closeX, buttonY + 2 }, "~U~ndo", cmUndo, bfNormal));
	insert(new TButton({ closeX, buttonY, right, buttonY + 2 }, "Close", cmCancel, bfNormal));
	_status = new CTuiTextView({ labelX, statusY, renameX - 1, statusY + 1 }, {});
	insert(_status);

	setInputText(_nameMask, _sessionRules.nameMask);
	setInputText(_extensionMask, _sessionRules.extensionMask);
	setInputText(_find, _sessionRules.find);
	setInputText(_replaceWith, _sessionRules.replaceWith);
	setChecked(_findOptions, { _sessionRules.findIsRegex, _sessionRules.findIsCaseSensitive });
	ushort letterCase = (ushort)_sessionRules.letterCase;
	_letterCase->setData(&letterCase);
	setInputText(_counterStart, QString::number(_sessionRules.counterStart));
	setInputText(_counterStep, QString::number(_sessionRules.counterStep));
	setInputText(_counterDigits, QString::number(_sessionRules.counterDigits));

	_session.setRules(_sessionRules);
	showSession();
	_nameMask->select();
}

void CTuiBatchRenameDialog::handleEvent(TEvent& event)
{
	TDialog::handleEvent(event);

	if (event.what == evCommand && (event.message.command == cmRename || event.message.command == cmUndo))
	{
		runStep(event.message.command == cmRename ? Step::Apply : Step::Undo);
		clearEvent(event);
	}

	if (BatchRenameRules fieldRules = rules(); fieldRules != _sessionRules)
	{
		_session.setRules(fieldRules);
		_sessionRules = std::move(fieldRules);
		showSession();
	}
}

BatchRenameRules CTuiBatchRenameDialog::rules() const
{
	// An empty or out of range number is the nearest one in range
	const auto numberIn = [](const TInputLine* input, const CBatchRenameSession::Range range) {
		return std::clamp(inputText(input).toInt(), range.min, range.max);
	};

	ushort letterCase = 0;
	_letterCase->getData(&letterCase);
	return {
		.nameMask = inputText(_nameMask),
		.extensionMask = inputText(_extensionMask),
		.letterCase = static_cast<LetterCase>(letterCase),
		.find = inputText(_find),
		.replaceWith = inputText(_replaceWith),
		.findIsRegex = isChecked(_findOptions, FindIsRegex),
		.findIsCaseSensitive = isChecked(_findOptions, FindIsCaseSensitive),
		.counterStart = numberIn(_counterStart, CBatchRenameSession::counterStartRange),
		.counterStep = numberIn(_counterStep, CBatchRenameSession::counterStepRange),
		.counterDigits = numberIn(_counterDigits, CBatchRenameSession::counterDigitsRange),
	};
}

void CTuiBatchRenameDialog::showSession()
{
	_preview->showSession();
	setStatus(_session.findPatternError());
	for (const auto [command, step] : { std::pair{ cmRename, Step::Apply }, std::pair{ cmUndo, Step::Undo } })
		_session.canRun(step) ? enableCommand(command) : disableCommand(command);
}

void CTuiBatchRenameDialog::runStep(const Step step)
{
	// A button pressed before Turbo Vision shows it disabled: keys typed ahead arrive before the redraw
	if (!_session.canRun(step))
		return;

	if (step == Step::Apply)
	{
		_nameMaskHistory.record(_sessionRules.nameMask);
		_extensionMaskHistory.record(_sessionRules.extensionMask);
		_findHistory.record(_sessionRules.find);
		_replaceWithHistory.record(_sessionRules.replaceWith);
	}

	setStatus(QStringLiteral("Renaming... Esc cancels"));
	TScreen::flushScreen(); // No events run until the renaming ends
	auto nextEscapeCheck = std::chrono::steady_clock::now();
	_session.run(step, [&nextEscapeCheck] {
		if (const auto now = std::chrono::steady_clock::now(); now >= nextEscapeCheck)
		{
			nextEscapeCheck = now + escapeCheckInterval;
			return !escapeTyped();
		}
		return true;
	});

	showSession();
}

void CTuiBatchRenameDialog::setStatus(const QString& text)
{
	_status->setLines({ elidedInTheMiddle(text, _status->size.x) });
}

} // namespace

void runBatchRenameDialog(const std::vector<CFileSystemObject>& items)
{
	auto* dialog = new CTuiBatchRenameDialog(TProgram::deskTop->getExtent(), items);
	TProgram::deskTop->execView(dialog);
	CBatchRenameSession::saveOptions(dialog->rules());
	TObject::destroy(dialog);
}
