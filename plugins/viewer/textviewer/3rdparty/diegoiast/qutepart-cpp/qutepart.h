#pragma once

/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * \file qutepart.h
 * \brief Main Qutepart header. Contains whole API.
 *
 * See also hl_factory.h if you need only syntax highlighter.
 */

#include <QColor>
#include <QDebug>
#include <QPlainTextEdit>
#include <QSharedPointer>
#include <QTextBlock>

class QSyntaxHighlighter;

namespace Qutepart {

// clang-format off
const int BOOMARK_BIT       = 1 << 0;
const int MODIFIED_BIT      = 1 << 1;
const int WARNING_BIT       = 1 << 2;
const int ERROR_BIT         = 1 << 3;
const int INFO_BIT          = 1 << 4;
const int BREAKPOINT_BIT    = 1 << 5;
const int EXECUTING_BIT     = 1 << 6;

// Future expansion
const int UNUSED9_BIT  = 1 << 7;
const int UNUSED8_BIT  = 1 << 8;
const int UNUSED7_BIT  = 1 << 9;
const int UNUSED6_BIT  = 1 << 10;
const int UNUSED5_BIT  = 1 << 11;
const int UNUSED4_BIT  = 1 << 12;
const int UNUSED3_BIT  = 1 << 13;
const int UNUSED2_BIT  = 1 << 14;
const int UNUSED1_BIT  = 1 << 15;
// clang-format on

QIcon iconForStatus(int status);

/**
 * \enum IndentAlg
 * \brief Indentation algorithm.
 * Returned by ::chooseLanguage().
 *
 * Passed to ::Qutepart::Qutepart::setIndentAlgorithm()
 */
enum IndentAlg {
	/// Do not apply any algorithm. Default text editor behaviour.
	INDENT_ALG_NONE = 0,
	/// Insert to new lines indentation equal to previous line.
	INDENT_ALG_NORMAL,
	/// Algorithm for C-style languages where curly brackets are used to mark code blocks. C, C++,
	/// PHP, Java, JS, ...
	INDENT_ALG_CSTYLE,
	/// Lisp indentation.
	INDENT_ALG_LISP,
	/// Scheme indentation.
	INDENT_ALG_SCHEME,
	/// XML indentation.
	INDENT_ALG_XML,
	/// Python indentation.
	INDENT_ALG_PYTHON,
	/// Ruby indentation.
	INDENT_ALG_RUBY,
};

/**
 * Programming language ID and related information.
 *
 * This structure is returned by ::chooseLanguage()
 */
struct LangInfo {
  public:
	LangInfo() = default;

	inline LangInfo(const QString &id, const QStringList &names, IndentAlg indentAlg)
		: id(id), names(names), indentAlg(indentAlg) {}

	/// Check if the struct is valid (filled with meaningfull info)
	inline bool isValid() const { return !id.isEmpty(); }

	/// Internal unique language ID. Pass to ::Qutepart::Qutepart::setHighlighter()
	QString id;

	/// User readable language names
	QStringList names;

	/// Indenter algorithm for the language. Pass to ::Qutepart::Qutepart::setIndentAlgorithm()
	IndentAlg indentAlg = INDENT_ALG_NONE;
};

/**
 * Choose language by available parameters.
 * First parameters have higher priority.
 * Returns `QString()` if can not detect the language.
 *
 * Fill as much parameters as you can. Set `QString()` for unknown parameters.
 *
 * \param mimeType The file MIME type. i.e. ``text/html``
 * \param languageName The language name as written in the <a
 * href="https://github.com/andreikop/qutepart-cpp/blob/master/src/hl/language_db_generated.cpp">language
 * DB</a> \param sourceFilePath The path to the file which is edited. \param firstLine Contents of
 * the first line of the file which is going to be edited.
 */
LangInfo chooseLanguage(const QString &mimeType = QString(),
						const QString &languageName = QString(),
						const QString &sourceFilePath = QString(),
						const QString &firstLine = QString());

class Indenter;
class BracketHighlighter;
class LineNumberArea;
class MarkArea;
class Minimap;
class Completer;
class Theme;
class FoldingArea;

/**
 * Document line.
 *
 * A convenience class to programmatically edit the document
 */
class Line {
  public:
	explicit Line(const QTextBlock &block);

	/// Get line text
	QString text() const;

	/// Get line length not including EOL symbol
	int length() const;

	/// Remove the line from the document
	void remove(int pos, int count);

	/// Get the line number
	int lineNumber() const;

  private:
	QTextBlock block_;
};

/** STL-compatible iterator implementation to work with document lines (blocks)
 *
 * Returns ::Qutepart::Line objects
 */
class LineIterator {
  public:
	explicit LineIterator(const QTextBlock &block);

	bool operator!=(const LineIterator &other);
	bool operator==(const LineIterator &other);
	LineIterator operator++();
	Line operator*();

  private:
	QTextBlock block_;
};

/** A convenience class which provides high level interface to work with
 * the document lines.
 *
 * Returned by ::Qutepart::Qutepart::lines()
 *
 * `Lines` is a performance-effective document representation.
 * Getting whole text of document with `QPlainTextEdit::toPlainText()`` requires a lot of memory
 * allocations and copying. This class accesses the text line by line without copying whole
 * document.
 */

class Lines {
  public:
	explicit Lines(QTextDocument *document);

	/// Line count in the document
	int count() const;

	/// Get line by index.
	Line at(int index) const;

	/// `begin()` method for STL iteration support
	LineIterator begin();

	/// `end()` method for STL iteration support
	LineIterator end();

	/// First line of the document
	Line first() const;

	/// Last line of the document
	Line last() const;

	/// Append line to the end of the document.
	void append(const QString &lineText);

	// Remove and return line at number. Return the text wo \n
	QString popAt(int lineNumber);

	// Insert at given line number one or more lines.
	// The text shoud be \n-separated. \n at end is interpreted as empty line.
	void insertAt(int lineNumber, const QString &text);

  private:
	QTextDocument *document_;
};

/** Cursor position
 *
 * A convenience class, which is more friendly than low level QTextCursor API.
 *
 * Returned by ::Qutepart::Qutepart::textCursorPosition()
 */
struct TextCursorPosition {
	TextCursorPosition(int line_, int column_) : line(line_), column(column_) {}

	friend bool operator==(const TextCursorPosition &a, const TextCursorPosition &b) {
		return a.line == b.line && a.column == b.column;
	}

	/// Current line. First line is 0.
	int line;

	/// Current column. First column is 0.
	int column;
};

using CompletionCallback = std::function<QSet<QString>(const QString &)>;

/**
  Code editor widget
*/
class Qutepart : public QPlainTextEdit {
	Q_OBJECT

  public:
	explicit Qutepart(QWidget *parent = nullptr, const QString &text = {});

	// Not copyable or movable
	Qutepart(const Qutepart &) = delete;
	Qutepart &operator=(const Qutepart &) = delete;
	Qutepart(Qutepart &&) = delete;
	Qutepart &operator=(Qutepart &&) = delete;

	virtual ~Qutepart();

	/// High-performance access to document lines. See ::Qutepart::Lines
	Lines lines() const;

	/**
	 * Set highlighter. Use `Qutepart::chooseLanguage()` to choose the language
	 *
	 * \param languageId Language name. See Qutepart::LangInfo::id.
	 */
	void setHighlighter(const QString &languageId);

	/**
	 * Removes the current syntax highlighter, all text will be drawen using default colors.
	 */
	void removeHighlighter();

	/**
	 * Set indenter algorithm. Use `Qutepart::chooseLanguage()` to choose the algorithm.
	 *
	 * \param indentAlg Algorithm name. See Qutepart::LangInfo::indentAlg.
	 */
	void setIndentAlgorithm(IndentAlg indentAlg);

	void setDefaultColors();
	void setTheme(const Theme *newTheme);
	const Theme *getTheme() const { return theme; }

	/// Convenience method to get text cursor position.
	TextCursorPosition textCursorPosition() const;

	/// Go to specified line and column. First line and first column have index 0
	void goTo(int line, int column = 0);
	/// Go to text position specified by ::Qutepart::TextCursorPosition
	void goTo(const TextCursorPosition &pos);

	/// Indent current line using current smart indentation algorithm
	void autoIndentCurrentLine();

	/// Use Tabs instead of spaces for indentation
	bool indentUseTabs() const;
	/// Use Tabs instead of spaces for indentation
	void setIndentUseTabs(bool);

	/// Indentation width. Count of inserted spaces, Tab symbol display width
	int indentWidth() const;
	/// Indentation width. Count of inserted spaces, Tab symbol display width
	void setIndentWidth(int);

	/// Visual option. Draw indentation symbols.
	bool drawIndentations() const;
	/// Visual option. Draw indentation symbols.
	void setDrawIndentations(bool);

	/// Visual option. Draw any whitespace symbol.
	bool drawAnyWhitespace() const;
	/// Visual option. Draw any whitespace symbol.
	void setDrawAnyWhitespace(bool);

	/// Visual option. Draw incorrent indentation. i.e. at end of line or Tab after spaces.
	bool drawIncorrectIndentation() const;
	/// Visual option. Draw incorrent indentation. i.e. at end of line or Tab after spaces.
	void setDrawIncorrectIndentation(bool);

	/// Visual option. Draw solid line length marker (usually after column 80)
	bool drawSolidEdge() const;
	/// Visual option. Draw solid line length marker (usually after column 80)
	void setDrawSolidEdge(bool);

	/// When passing the line edge, should the word be moved to the next line (see line length edge)
	bool softLineWrapping() const;
	/// Enable/disable soft wrapping
	void setSoftLineWrapping(bool);

	/// Visual option. Column on which line lendth marker is drawn.
	int lineLengthEdge() const;
	/// Visual option. Column on which line lendth marker is drawn.
	void setLineLengthEdge(int);

	/// Visual option. Color of line lendth edge.
	QColor lineLengthEdgeColor() const;
	/// Visual option. Color of line lendth edge.
	void setLineLengthEdgeColor(QColor);

	/// Visual option. Color of current line highlighting. `QColor()` if disabled.
	QColor currentLineColor() const;
	/// Visual option. Color of current line highlighting. `QColor()` if disabled.
	void setCurrentLineColor(QColor);

	/// Smart folding: when you ask to fold a block, it its already foldede
	/// it will fold the parent block. When unfolding, all child blocks will be unfolded.
	/// If disabled, fold/unfold work on the current block only.
	bool smartFolding() const;
	/// Enable or disable smart folding.
	void setSmartFolding(bool enabled);

	bool bracketHighlightingEnabled() const;
	void setBracketHighlightingEnabled(bool value);

	bool lineNumbersVisible() const;
	void setLineNumbersVisible(bool value);

	bool minimapVisible() const;
	void setMinimapVisible(bool value);

	/// To to logical, or phisical end/start of line.
	bool getSmartHomeEnd() const;
	/// To to logical, or phisical end/start of line.
	void setSmartHomeEnd(bool value);

	/// When cursor moves, select the current word under cursor in all the document
	void setMarkCurrentWord(bool enable);
	bool getMarkCurrentWord() const;

	/// When pressing '[' - should we enclose the selection with '[]'? Supports for quotes etc
	void setBracketAutoEnclose(bool enable);
	bool getBracketAutoEnclose() const;

	// Autocompletion
	void setCompletionEnabled(bool);
	bool completionEnabled() const;
	void setCompletionThreshold(int);
	int completionThreshold() const;
	/// User defined completion callback, used to fill suggestions to user
	inline void setCompletionCallback(CompletionCallback callback) {
		completionCallback_ = callback;
	}

	void removeMetaData();

	/// Returns the status of a line. A line is marked as modified when its changed via the user
	bool isLineModified(int lineNumber) const;
	/// Set the status of a line, modified or not
	void setLineModified(int lineNumber, bool modified) const;
	/// Set the status of a line, modified or not
	void setLineModified(QTextBlock &block, bool modified) const;
	/// Clear modifications from all document.
	void removeModifications();

	// Markings
	void modifyBlockFlag(int lineNumber, int bit, bool status, QColor background);
	bool getBlockFlag(int lineNumber, int bit) const;

	bool getLineBookmark(int lineNumber) const;
	void setLineBookmark(int lineNumber, bool status);
	bool getLineWarning(int lineNumber) const;
	void setLineWarning(int lineNumber, bool status);
	bool getLineError(int lineNumber) const;
	void setLineError(int lineNumber, bool status);
	bool getLineInfo(int lineNumber) const;
	void setLineInfo(int lineNumber, bool status);
	bool getLineBreakpoint(int lineNumber) const;
	void setLineBreakpoint(int lineNumber, bool status);
	bool getLineExecuting(int lineNumber) const;
	void setLineExecuting(int lineNumber, bool status);
	void setLineMessage(int lineNumber, const QString &message);

	auto getColorForLineFlag(int flag) -> QColor;
	auto fixLineFlagColors() -> void;

	// Actions
	inline QAction *increaseIndentAction() const { return increaseIndentAction_; }
	inline QAction *decreaseIndentAction() const { return decreaseIndentAction_; }
	inline QAction *toggleBookmarkAction() const { return toggleBookmarkAction_; }
	inline QAction *prevBookmarkAction() const { return prevBookmarkAction_; }
	inline QAction *nextBookmarkAction() const { return nextBookmarkAction_; }
	inline QAction *invokeCompletionAction() const { return invokeCompletionAction_; }
	inline QAction *scrollDownAction() const { return scrollDownAction_; }
	inline QAction *scrollUpAction() const { return scrollUpAction_; }
	inline QAction *duplicateSelectionAction() const { return duplicateSelectionAction_; }
	inline QAction *moveLineUpAction() const { return moveLineUpAction_; }
	inline QAction *moveLineDownAction() const { return moveLineDownAction_; }
	inline QAction *deleteLineAction() const { return deleteLineAction_; }
	inline QAction *cutLineAction() const { return cutLineAction_; }
	inline QAction *copyLineAction() const { return copyLineAction_; }
	inline QAction *pasteLineAction() const { return pasteLineAction_; }
	inline QAction *insertLineAboveAction() const { return insertLineAboveAction_; }
	inline QAction *insertLineBelowAction() const { return insertLineBelowAction_; }
	inline QAction *joinLinesAction() const { return joinLinesAction_; }
	inline QAction *foldAction() const { return foldAction_; };
	inline QAction *unfoldAction() const { return unfoldAction_; };
	inline QAction *toggleFoldAction() const { return toggleFoldAction_; }
	inline QAction *foldTopLevelAction() const { return foldTopLevelAction_; }
	inline QAction *unfoldAllAction() const { return unfoldAllAction_; }

	/// Zoom In the document by scaling fonts
	inline QAction *zoomInAction() const { return zoomInAction_; }
	/// Zoom Out the document by scaling fonts
	inline QAction *zoomOutAction() const { return zoomOutAction_; }
	/// Comment the current line, or selected text
	inline QAction *toggleCommentAction() const { return toggleActionComment_; }
	/// Find matching bracket for this position
	inline QAction *findMatchingBracketAction() const { return findMatchingBracketAction_; }

	/// Returns a list of folded line numbers
	QVector<int> getFoldedLines() const;

	/// Restores the folding state from a list of line numbers
	void setFoldedLines(const QVector<int> &foldedLines);

	/// Fold the block at this line, fail silently
	void foldBlock(int lineNumber);

	/// Unfold the block at this line, fail silently
	void unfoldBlock(int lineNumber);

	/// Toggle the block at this line, fail silently
	void toggleFold(int lineNumber);

	/// Fold the current block
	void foldCurrentBlock();

	/// Unfold the current block
	void unfoldCurrentBlock();

	/// Toggle folding of the current block
	void toggleCurrentFold();

	/// Fold all top-level blocks, with special handling for a single top-level block
	void foldTopLevelBlocks();

	/// Unfold all folded blocks in the document
	void unfoldAll();

	// Convenience functions
	void resetSelection();

	void multipleCursorPaste();

	/**
	 * \brief Copies selected text or full lines under cursor(s), like VSCode.
	 *  - If any cursor/selection exists, collects selected text or lines under the cursors,
	 *   and joins them with '\n' to the clipboard.
	 * - If no cursor has a selection, copies the whole line(s) under all cursors to the clipboard,
	 *   joined with '\n' (block mode copy like VSCode).
	 * - No changes made to the document or caret.
	 */
	void multipleCursorCopy();

	/**
	 * Cuts selected text or whole lines with multi-cursor support.
	 * When there is no selection, cuts the whole line under each cursor,
	 * merges all cut text (in order) into the clipboard (like VSCode),
	 * and places the caret after the cut region (or at the last line).
	 */
	void multipleCursorCut();

  protected:
	void keyPressEvent(QKeyEvent *event) override;
	void keyReleaseEvent(QKeyEvent *event) override;
	void paintEvent(QPaintEvent *event) override;
	void changeEvent(QEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;

  private:
	QList<QTextEdit::ExtraSelection> persitentSelections;
	QList<QTextCursor> extraCursors;
	QTimer *extraCursorBlinkTimer_ = nullptr;
	bool extraCursorsVisible_ = true;

	void initActions();
	QAction *createAction(const QString &text, QKeySequence shortcut, const QString &iconFileName,
						  std::function<void()> const &handler);

	QList<QTextEdit::ExtraSelection> highlightText(const QString &word, bool fullWords);
	// whitespace and edge drawing
	void drawIndentMarkersAndEdge(const QRect &rect);
	void drawIndentMarker(QPainter *painter, QTextBlock block, int column);
	void drawEdgeLine(QPainter *painter, QTextBlock block, int edgePos);
	void drawWhiteSpace(QPainter *painter, QTextBlock block, int column, QChar ch);
	int effectiveEdgePos(const QString &text);
	void chooseVisibleWhitespace(const QString &text, QVector<bool> *result);

	QTextEdit::ExtraSelection currentLineExtraSelection() const;

	void resizeEvent(QResizeEvent *event) override;

	void updateTabStopWidth();

	QRect cursorRect(QTextBlock block, int column, int offset) const;
	void gotoBlock(const QTextBlock &block);

	void indentBlock(const QTextBlock &block, bool withSpace) const;
	void unIndentBlock(const QTextBlock &block, bool withSpace) const;
	void changeSelectedBlocksIndent(bool increase, bool withSpace);

	QTextBlock findBlockToFold(QTextBlock currentBlock);
	void setBlockFolded(QTextBlock &block, bool folded);

	void scrollByOffset(int offset);

	void duplicateSelection();
	void moveSelectedLines(int offsetLines);

	void deleteLine();

	void cutLine();
	void copyLine();
	void pasteLine();

	void insertLineAbove();
	void insertLineBelow();
	void toggleComment();

	QTextCursor applyOperationToAllCursors(
		std::function<void(QTextCursor &)> operation,
		std::function<bool(const QTextCursor &, const QTextCursor &)> sortOrderBeforeOp);

  private slots:
	void updateViewport();
	void updateExtraSelections();

	void onShortcutHome(QTextCursor::MoveMode moveMode);
	void onShortcutEnd(QTextCursor::MoveMode moveMode);

	void onShortcutToggleBookmark();
	void onShortcutPrevBookmark();
	void onShortcutNextBookmark();

	void joinNextLine(QTextCursor &cursor);
	void onShortcutJoinLines();

	void toggleExtraCursorsVisibility();

  private:
	CompletionCallback completionCallback_;
	const Theme *theme = nullptr;

	QTimer *currentWordTimer;
	QString lastWordUnderCursor;

	QSyntaxHighlighter *highlighter_ = nullptr;
	Indenter *indenter_;
	BracketHighlighter *bracketHighlighter_ = nullptr;
	LineNumberArea *lineNumberArea_ = nullptr;
	MarkArea *markArea_ = nullptr;
	Minimap *miniMap_ = nullptr;
	Completer *completer_;
	FoldingArea *foldingArea_ = nullptr;

	bool drawIndentations_;
	bool drawAnyWhitespace_;
	bool drawIncorrectIndentation_;
	bool drawSolidEdge_;
	bool enableSmartHomeEnd_;
	bool softLineWrapping_;
	bool smartFolding_ = true;

	int lineLengthEdge_;
	QColor lineNumberColor;
	QColor currentLineNumberColor;
	QColor lineLengthEdgeColor_;
	QColor currentLineColor_;
	QColor indentColor_;
	QColor whitespaceColor_;

	bool brakcetsQutoEnclose;
	bool completionEnabled_;
	int completionThreshold_;
	int viewportMarginStart_;
	int viewportMarginEnd_;

	// private, not API
	QAction *homeAction_;
	QAction *homeSelectAction_;
	QAction *endAction_;
	QAction *endSelectAction_;
	QAction *increaseIndentAction_;
	QAction *decreaseIndentAction_;
	QAction *toggleBookmarkAction_;
	QAction *prevBookmarkAction_;
	QAction *nextBookmarkAction_;
	QAction *invokeCompletionAction_;
	QAction *scrollDownAction_;
	QAction *scrollUpAction_;
	QAction *duplicateSelectionAction_;
	QAction *moveLineUpAction_;
	QAction *moveLineDownAction_;
	QAction *deleteLineAction_;
	QAction *cutLineAction_;
	QAction *copyLineAction_;
	QAction *pasteLineAction_;
	QAction *insertLineAboveAction_;
	QAction *insertLineBelowAction_;
	QAction *joinLinesAction_;
	QAction *zoomInAction_;
	QAction *zoomOutAction_;
	QAction *toggleActionComment_;
	QAction *findMatchingBracketAction_;
	QAction *foldAction_ = nullptr;
	QAction *unfoldAction_ = nullptr;
	QAction *toggleFoldAction_ = nullptr;
	QAction *foldTopLevelAction_ = nullptr;
	QAction *unfoldAllAction_ = nullptr;

	friend class LineNumberArea;
	friend class MarkArea;
	friend class FoldingArea;

  public:
	int MaxLinesForWordHighligher = 100000;
};

/**
A helper class which allows to group edit operations on Qutepart using RAII approach.
Operations are undo-redoble as a single change.
Example:

	{
		AtomicEditOperation op(qutepart);
		qutepart.lines().insertAt(3, "line three");
		qutepart.lines().insertAt(4, "line four");
	}
 */
class AtomicEditOperation {
  public:
	explicit AtomicEditOperation(Qutepart *qutepart);
	~AtomicEditOperation();

  private:
	Qutepart *qutepart_;
};

} // namespace Qutepart
