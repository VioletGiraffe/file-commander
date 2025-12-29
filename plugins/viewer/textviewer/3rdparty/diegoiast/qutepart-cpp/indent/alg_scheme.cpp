/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/*
This indenter works according to
    http://community.schemewiki.org/?scheme-style

TODO support (module)
 */

#include <QDebug>

#include "text_block_utils.h"

#include "alg_scheme.h"

namespace Qutepart {

namespace {

/* Move backward to the start of the word at the end of a string.
 * Return the word
 */
QString lastWord(const QString &text) {
    for (int i = text.length() - 1; i >= 0; i--) {
        QChar ch = text[i];
        if (ch.isSpace() || ch == '(' || ch == ')') {
            return text.mid(i + 1);
        }
    }

    return text;
}

// Find end of the last expression
TextPosition findExpressionEnd(QTextBlock block) {
    while (block.isValid()) {
        int column = lastNonSpaceColumn(block.text());
        if (column > 0) {
            return TextPosition(block, column);
        }
        block = block.previous();
    }

    return TextPosition();
}

// Find start of not finished expression
TextPosition findExpressionStart(QTextBlock block) {
    TextPosition expEnd = findExpressionEnd(block);

    QString text = expEnd.block.text().left(expEnd.column + 1);

    if (text.endsWith(")")) {
        return findOpeningBracketBackward('(', expEnd);
    } else {
        return TextPosition(expEnd.block, text.length() - lastWord(text).length());
    }
}

} // anonymous namespace

QString IndentAlgScheme::computeSmartIndent(QTextBlock block, int /*cursorPos*/) const {
    TextPosition expStart = findExpressionStart(block.previous());

    if (!expStart.isValid()) {
        return "";
    }

    QString blockText = expStart.block.text();
    QString expression = stripRightWhitespace(blockText.mid(expStart.column));
    QString beforeExpression = blockText.left(expStart.column).trimmed();

    if (beforeExpression.startsWith("(module")) { // special case
        return "";
    } else if (beforeExpression.endsWith("define")) { // special case
        return QString().fill(' ', beforeExpression.length() - QString("define").length() + 1);
    } else if (beforeExpression.endsWith("let")) { // special case
        return QString().fill(' ', beforeExpression.length() - QString("let").length() + 1);
    } else {
        return QString().fill(' ', expStart.column);
    }
}

} // namespace Qutepart
