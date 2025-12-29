/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "hl/text_type.h"
#include "text_block_utils.h"

#include "indent_funcs.h"

namespace Qutepart {

QString lineIndent(const QString &line) { return line.left(firstNonSpaceColumn(line)); }

QString blockIndent(QTextBlock block) { return lineIndent(block.text()); }

void setBlockIndent(QTextCursor *cursor, const QString &indent) {
    int currentIndentLength = blockIndent(cursor->block()).size();
    setPositionInBlock(cursor, 0, QTextCursor::MoveAnchor);
    setPositionInBlock(cursor, currentIndentLength, QTextCursor::KeepAnchor);
    cursor->insertText(indent);
}

QString prevBlockIndent(QTextBlock block) {
    QTextBlock prevBlock = block.previous();

    if (!block.isValid()) {
        return QString();
    }

    return blockIndent(prevBlock);
}

QString increaseIndent(const QString &line, const QString &indent) { return indent + line; }

QString decreaseIndent(const QString &line, const QString &indent) {
    if (line.startsWith(indent)) {
        return line.mid(indent.length());
    } else { // oops, strange indentation, just return previous indent
        return line;
    }
}

QString makeIndent(int confWidth, bool confUseTabs) {
    if (confUseTabs) {
        return QString("\t");
    } else {
        return QString().fill(' ', confWidth);
    }
}

/* Make indent text with specified with.
 * Contains width count of spaces, or tabs and spaces
 */
QString makeIndentFromWidth(int width, int confWidth, bool confUseTabs) {
    if (confUseTabs) {
        int tabCount = width / confWidth;
        int spaceCount = width % confWidth;
        return QString().fill('\t', tabCount) + QString().fill(' ', spaceCount);
    } else {
        return QString().fill(' ', width);
    }
}

QString makeIndentAsColumn(QTextBlock block, int column, int confIndentWidth, bool confUseTabs,
                           int offset) {
    QString blockText = block.text();
    QString textBeforeColumn = blockText.left(column);
    int tabCount = textBeforeColumn.count('\t');

    int visibleColumn = column + (tabCount * (confIndentWidth - 1));
    return makeIndentFromWidth(visibleColumn + offset, confIndentWidth, confUseTabs);
}

QString prevNonEmptyBlockIndent(const QTextBlock &block) {
    return blockIndent(prevNonEmptyBlock(block));
}

QString textWithCommentsWiped(const QTextBlock &block) {
    QString text = block.text();
    QString typeMap = textTypeMap(block);
    for (int i = 0; i < text.length(); i++) {
        if (typeMap[i] == 'c') {
            text[i] = ' ';
        }
    }

    return text;
}

QChar firstNonSpaceChar(const QTextBlock &block) {
    QString textStripped = stripLeftWhitespace(block.text());
    if (textStripped.isEmpty()) {
        return QChar();
    } else {
        return textStripped[0];
    }
}

QChar lastNonSpaceChar(const QTextBlock &block) {
    QString textStripped = stripRightWhitespace(block.text());
    if (textStripped.isEmpty()) {
        return QChar();
    } else {
        return textStripped[textStripped.length() - 1];
    }
}

} // namespace Qutepart
