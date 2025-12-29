/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include <QDebug>

#include "indent_funcs.h"

#include "alg_python.h"

namespace Qutepart {

namespace {

const QStringList KEYWORDS = {"continue", "break", "pass", "raise", "return"};

const QString CLOSING_BRACKETS = ")]}";

QChar last(const QString &str) { return str[str.length() - 1]; }

} // namespace

// Compute smart indent for case when cursor is on pos
QString IndentAlgPython::computeSmartIndent(const TextPosition &pos) const {
    QString lineStripped =
        pos.block.text().left(pos.column).trimmed(); // empty text from invalid block is ok
    int spaceLen = firstNonSpaceColumn(pos.block.text());

    /* Move initial search position to bracket start, if bracket was closed
    l = [1,
         2]|
    */
    if ((!lineStripped.isEmpty()) && CLOSING_BRACKETS.contains(last(lineStripped))) {
        TextPosition foundPos = findOpeningBracketBackward(
            last(lineStripped), TextPosition(pos.block, spaceLen + lineStripped.length() - 1));
        if (foundPos.isValid()) {
            return computeSmartIndent(foundPos);
        }
    }

    /* Unindent if hanging indentation finished
    func(a,
         another_func(a,
                      b),|
     */
    QChar secondLast = lineStripped[lineStripped.length() - 2];

    if (lineStripped.length() > 1 && last(lineStripped) == ',' &&
        CLOSING_BRACKETS.contains(secondLast)) {
        TextPosition foundPos = findOpeningBracketBackward(
            secondLast,
            TextPosition(pos.block,
                         stripRightWhitespace(pos.block.text().left(pos.column)).length() - 2));
        if (foundPos.isValid()) {
            return computeSmartIndent(foundPos);
        }
    }

    /* Check hanging indentation
    call_func(x,
              y,
              z
    But
    call_func(x,
        y,
        z
    */
    TextPosition foundPos = findAnyOpeningBracketBackward(pos);
    // indent this way only line, which contains 'y', not 'z'
    if (foundPos.block.blockNumber() == pos.block.blockNumber()) {
        return makeIndentAsColumn(foundPos.block, foundPos.column + 1, width_, useTabs_);
    }

    // finally, a raise, pass, and continue should unindent
    if (KEYWORDS.contains(lineStripped) || lineStripped.startsWith("raise ") ||
        lineStripped.startsWith("return ")) {
        return decreaseIndent(blockIndent(pos.block), indentText());
    }

    /*
    for:

    func(a,
         b):
     */
    if (lineStripped.endsWith(':')) {
        int newColumn = spaceLen + lineStripped.length() - 1;
        QString prevIndent = computeSmartIndent(TextPosition(pos.block, newColumn));
        return increaseIndent(prevIndent, indentText());
    }

    /* Generally, when a brace is on its own at the end of a regular line
    (i.e a data structure is being started), indent is wanted.
    For example:
    dictionary = {
        'foo': 'bar',
    }
    */
    if (lineStripped.endsWith("{[")) {
        return increaseIndent(blockIndent(pos.block), indentText());
    }

    return blockIndent(pos.block);
}

QString IndentAlgPython::computeSmartIndent(QTextBlock block, int /*cursorPos*/) const {
    QTextBlock nonEmpty = prevNonEmptyBlock(block);
    int column = nonEmpty.text().length();
    return computeSmartIndent(TextPosition(nonEmpty, column));
}

} // namespace Qutepart
