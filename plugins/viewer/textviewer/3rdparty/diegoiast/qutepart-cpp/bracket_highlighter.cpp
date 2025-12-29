/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include <QDebug>
#include <qutepart.h>
#include <theme.h>

#include "bracket_highlighter.h"
#include "hl/text_type.h"
#include "text_block_utils.h"

namespace Qutepart {

namespace {

const QString START_BRACKETS = "({[";
const QString END_BRACKETS = ")}]";
const QString ALL_BRACKETS = START_BRACKETS + END_BRACKETS;

} // anonymous namespace

BracketHighlighter::BracketHighlighter(Qutepart *q) { qpart = q; }

QList<QTextEdit::ExtraSelection> BracketHighlighter::highlightBracket(QChar bracket,
                                                                      const TextPosition &pos) {
    TextPosition matchingPos;

    if (START_BRACKETS.contains(bracket)) {
        matchingPos = findClosingBracketForward(bracket, pos);
    } else {
        matchingPos = findOpeningBracketBackward(bracket, pos);
    }

#if 0 // TODO timeout
    if ( ! matchingPos.isValid()) {
        return QList<QTextEdit::ExtraSelection>();
    }
#endif

    cachedBracket_ = pos;
    cachedMatchingBracket_ = matchingPos;

    QList<QTextEdit::ExtraSelection> result;
    if (matchingPos.isValid()) {
        result.append(makeMatchSelection(pos, true));
        result.append(makeMatchSelection(matchingPos, true));
    } else {
        result.append(makeMatchSelection(pos, false));
    }

    return result;
}

QList<QTextEdit::ExtraSelection> BracketHighlighter::extraSelections(const TextPosition &pos) {
    QString blockText = pos.block.text();

    if (pos.column < blockText.length() && ALL_BRACKETS.contains(blockText[pos.column]) &&
        isCode(pos.block, pos.column)) {
        return highlightBracket(blockText[pos.column], pos);
    } else if (pos.column > 0 && ALL_BRACKETS.contains(blockText[pos.column - 1]) &&
               isCode(pos.block, pos.column - 1)) {
        TextPosition newPos = pos;
        newPos.column -= 1;
        return highlightBracket(blockText[pos.column - 1], newPos);
    } else {
        return QList<QTextEdit::ExtraSelection>();
    }
}

// Make matched or unmatched QTextEdit.ExtraSelection
QTextEdit::ExtraSelection BracketHighlighter::makeMatchSelection(const TextPosition &pos,
                                                                 bool matched) {
    QTextEdit::ExtraSelection selection;
    if (qpart) {
        auto palette = qpart->palette();
        auto matchedColor = palette.color(QPalette::Highlight).lighter();
        auto nonMatchedColor = palette.color(QPalette::LinkVisited).lighter();

        if (auto theme = qpart->getTheme()) {
            if (theme->getEditorColors().contains(Theme::Colors::BracketMatching)) {
                matchedColor = theme->getEditorColors()[Theme::Colors::BracketMatching];
                nonMatchedColor = theme->getEditorColors()[Theme::Colors::MarkError];
            }
        }

        if (matched) {
            selection.format.setBackground(matchedColor);
        } else {
            selection.format.setBackground(nonMatchedColor);
        }
    }

    selection.cursor = QTextCursor(pos.block);
    selection.cursor.setPosition(pos.block.position() + pos.column);
    selection.cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor);

    return selection;
}

} // namespace Qutepart
