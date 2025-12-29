/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include <QDebug>
#include <QRegularExpression>

#include "hl/text_type.h"
#include "indent_funcs.h"

#include "alg_ruby.h"

namespace Qutepart {

namespace {

// Unindent lines that match this regexp
const QRegularExpression rxUnindent("^\\s*((end|when|else|elsif|rescue|ensure)\\b|[\\]\\}])(.*)$");

// Indent after lines that match this regexp
const QRegularExpression rxIndent("^\\s*(def|if|unless|for|while|until|class|module|else|elsif|"
                                  "case|when|begin|rescue|ensure|catch)\\b");

const QRegularExpression rxBlockEnd("\\s*end$");

bool isBlockContinuing(QTextBlock block) { return block.text().endsWith('\\'); }

/* Returns the column with a non-whitespace characters
starting at the given cursor position and searching forwards.
*/
int nextNonSpaceColumn(QTextBlock block, int column) {
    QString textAfter = block.text().mid(column);
    if (!textAfter.trimmed().isEmpty()) {
        int spaceLen = textAfter.length() - stripLeftWhitespace(textAfter).length();
        return column + spaceLen;
    } else {
        return -1;
    }
}

} // anonymous namespace

RubyStatement::RubyStatement(QTextBlock startBlock, QTextBlock endBlock)
    : startBlock(startBlock), endBlock(endBlock) {}

QString RubyStatement::toString() const {
    return QString("{ %1, %2}").arg(startBlock.blockNumber()).arg(endBlock.blockNumber());
}

TextPosition RubyStatement::offsetToTextPos(int offset) const {
    QTextBlock block = startBlock;
    while (block != endBlock.next() && block.text().length() < offset) {
        offset -= block.text().length();
        block = block.next();
    }

    return TextPosition(block, offset);
}

// Return document.isCode at the given offset in a statement
bool RubyStatement::isPosCode(int offset) const {
    const TextPosition pos = offsetToTextPos(offset);
    return isCode(pos.block, pos.column);
}

// Return document.isComment at the given offset in a statement
bool RubyStatement::isPosComment(int offset) const {
    TextPosition pos = offsetToTextPos(offset);
    return isComment(pos.block, pos.column);
}

// Return the indent at the beginning of the statement
QString RubyStatement::indent() const { return blockIndent(startBlock); }

// Return the content of the statement from the document
QString RubyStatement::content() const {
    if (!contentCache_.isNull()) {
        return contentCache_;
    }

    QString cnt;

    QTextBlock block = startBlock;

    while (block != endBlock.next()) {
        QString text = textWithCommentsWiped(block);
        if (text.endsWith('\\')) {
            cnt += text.left(text.length() - 1);
            cnt += ' ';
        } else {
            cnt += text;
        }
        block = block.next();
    }

    contentCache_ = cnt;
    return cnt;
}

const QString &IndentAlgRuby::triggerCharacters() const {
    static QString chars = "cdefhilnrsuw}]";
    return chars;
}

bool IndentAlgRuby::isCommentBlock(QTextBlock block) const {
    QString text(block.text());
    int firstColumn = firstNonSpaceColumn(text);
    return (firstColumn == -1) || isComment(block, firstColumn);
}

/* Return the closest non-empty line, ignoring comments
(result <= line). Return -1 if the document
*/
QTextBlock IndentAlgRuby::prevNonCommentBlock(QTextBlock block) const {
    block = prevNonEmptyBlock(block);
    while (block.isValid() && isCommentBlock(block)) {
        block = prevNonEmptyBlock(block);
    }
    return block;
}

/* Return true if the given column is at least equal to the column that
contains the last non-whitespace character at the given line, or if
the rest of the line is a comment.
*/
bool IndentAlgRuby::isLastCodeColumn(QTextBlock block, int column) const {
    return column >= lastNonSpaceColumn(block.text()) ||
           isComment(block, nextNonSpaceColumn(block, column + 1));
}

// Is there an open parenthesis?
bool IndentAlgRuby::isStmtContinuing(QTextBlock block) const {
    if (findAnyOpeningBracketBackward(TextPosition(block, block.length())).isValid()) {
        return true;
    }

    QString text = textWithCommentsWiped(block);

    QRegularExpression rx("(\\+|\\-|\\*|\\/|\\=|&&|\\|\\||\\band\\b|\\bor\\b|,)\\s*$");
    QRegularExpressionMatch match = rx.match(text);

    return match.hasMatch() && isCode(block, match.capturedStart());
}

/* Return the first line that is not preceded by a "continuing" line.
Return currBlock if currBlock <= 0
*/
QTextBlock IndentAlgRuby::findStmtStart(QTextBlock block) const {
    QTextBlock prevBlock = prevNonCommentBlock(block);
    while (prevBlock.isValid() &&
           (((prevBlock == block.previous()) && isBlockContinuing(prevBlock)) ||
            isStmtContinuing(prevBlock))) {
        block = prevBlock;
        prevBlock = prevNonCommentBlock(block);
    }
    return block;
}

/* check if the trigger characters are in the right context,
otherwise running the indenter might be annoying to the user
*/
bool IndentAlgRuby::isValidTrigger(QTextBlock block) const {
    if (block.text().isEmpty()) { // new line
        return true;
    }

    QRegularExpressionMatch match = rxUnindent.match(block.text());
    return match.hasMatch() && match.captured(3).isEmpty();
}

/* Returns a tuple that contains the first and last line of the
previous statement before line.
*/
RubyStatement IndentAlgRuby::findPrevStmt(QTextBlock block) const {
    QTextBlock stmtEnd = prevNonCommentBlock(block);
    QTextBlock stmtStart = findStmtStart(stmtEnd);
    return RubyStatement(stmtStart, stmtEnd);
}

bool IndentAlgRuby::isBlockStart(const RubyStatement &stmt) const {
    QString content = stmt.content();
    if (rxIndent.match(content).hasMatch()) {
        return true;
    }

    QRegularExpression rx("((\\bdo\\b|\\{)(\\s*\\|.*\\|)?\\s*)$");

    return rx.match(content).hasMatch();
}

bool IndentAlgRuby::isBlockEnd(const RubyStatement &stmt) const {
    return rxUnindent.match(stmt.content()).hasMatch();
}

RubyStatement IndentAlgRuby::findBlockStart(QTextBlock block) const {
    int nested = 0;
    RubyStatement stmt(block, block);
    while (true) {
        if (!stmt.startBlock.isValid()) {
            return stmt;
        }

        stmt = findPrevStmt(stmt.startBlock);
        if (isBlockEnd(stmt)) {
            nested += 1;
        }

        if (isBlockStart(stmt)) {
            if (nested == 0) {
                return stmt;
            } else {
                nested -= 1;
            }
        }
    }
}

QString IndentAlgRuby::computeSmartIndent(QTextBlock block, int /*cursorPos*/) const {
    if (!isValidTrigger(block)) {
        return QString();
    }

    RubyStatement prevStmt = findPrevStmt(block);
    if (!prevStmt.endBlock.isValid()) {
        return QString(); // Can't indent the first line
    }

    QTextBlock prevBlock = prevNonEmptyBlock(block);

    // HACK Detect here documents
    if (isHereDoc(prevBlock, prevBlock.length() - 2)) {
        return QString();
    }

    // HACK Detect embedded comments
    if (isBlockComment(prevBlock, prevBlock.length() - 2)) {
        return QString();
    }

    QString prevStmtContent = prevStmt.content();
    QString prevStmtIndent = prevStmt.indent();

    // Are we inside a parameter list, array or hash?

    TextPosition openingBracketPos = findAnyOpeningBracketBackward(TextPosition(block, 0));

    if (openingBracketPos.isValid()) {
        bool shouldIndent = (openingBracketPos.block == prevStmt.endBlock) ||
                            QRegularExpression(",\\s*$").match(prevStmtContent).hasMatch();

        if ((!isLastCodeColumn(openingBracketPos.block, openingBracketPos.column)) ||
            findAnyOpeningBracketBackward(openingBracketPos).isValid()) {
            // TODO This is alignment, should force using spaces instead of
            // tabs:
            if (shouldIndent) {
                openingBracketPos.column += 1;
                int nextCol = nextNonSpaceColumn(openingBracketPos.block, openingBracketPos.column);
                if (nextCol > 0 && (!isComment(openingBracketPos.block, nextCol))) {
                    openingBracketPos.column = nextCol;
                }
            }

            // Keep indent of previous statement, while aligning to the anchor
            // column
            if (prevStmtIndent.length() > openingBracketPos.column) {
                return prevStmtIndent;
            } else {
                return makeIndentAsColumn(openingBracketPos.block, openingBracketPos.column, width_,
                                          useTabs_);
            }
        } else {
            QString indent = blockIndent(openingBracketPos.block);
            if (shouldIndent) {
                indent = increaseIndent(indent, indentText());
            }
            return indent;
        }
    }

    // Handle indenting of multiline statements.
    if ((prevStmt.endBlock == block.previous() && isBlockContinuing(prevStmt.endBlock)) ||
        isStmtContinuing(prevStmt.endBlock)) {
        if (prevStmt.startBlock == prevStmt.endBlock) {
            if (blockIndent(block).length() > blockIndent(prevStmt.endBlock).length()) {
                // Don't force a specific indent level when aligning manually
                return QString();
            }
            return increaseIndent(increaseIndent(prevStmtIndent, indentText()), indentText());
        } else {
            return blockIndent(prevStmt.endBlock);
        }
    }

    if (rxUnindent.match(block.text()).hasMatch()) {
        RubyStatement startStmt = findBlockStart(block);
        if (startStmt.startBlock.isValid()) {
            return startStmt.indent();
        } else {
            return QString();
        }
    }

    if (isBlockStart(prevStmt) && (!rxBlockEnd.match(prevStmt.content()).hasMatch())) {
        return increaseIndent(prevStmtIndent, indentText());
    } else if (QRegularExpression("[\\[\\{]\\s*$").match(prevStmtContent).hasMatch()) {
        return increaseIndent(prevStmtIndent, indentText());
    }

    // Keep current
    return prevStmtIndent;
}

} // namespace Qutepart
