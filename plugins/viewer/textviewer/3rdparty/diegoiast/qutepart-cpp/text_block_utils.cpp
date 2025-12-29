/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include <map>
#include <utility>

#include <QDebug>

#include "char_iterator.h"

#include "text_block_utils.h"

namespace Qutepart {

int firstNonSpaceColumn(const QString &line) {
    for (int i = 0; i < line.size(); i++) {
        if (!line[i].isSpace()) {
            return i;
        }
    }
    return line.size();
}

int lastNonSpaceColumn(const QString &line) {
    for (int i = line.size() - 1; i >= 0; i--) {
        if (!line[i].isSpace()) {
            return i;
        }
    }
    return 0;
}

int spaceAtEndCount(const QString &text) {
    int i = text.length() - 1;
    while (i >= 0 && text[i] == ' ') {
        i--;
    }

    return i + 1;
}

QString stripLeftWhitespace(const QString &line) { return line.mid(firstNonSpaceColumn(line)); }

QString stripRightWhitespace(const QString &line) {
    return line.left(lastNonSpaceColumn(line) + 1);
}

QTextBlock prevNonEmptyBlock(QTextBlock block) {
    if (!block.isValid()) {
        return QTextBlock();
    }

    block = block.previous();
    while (block.isValid() && block.text().isEmpty()) {
        block = block.previous();
    }

    return block;
}

QTextBlock nextNonEmptyBlock(QTextBlock block) {
    if (!block.isValid()) {
        return block;
    }

    block = block.next();

    while (block.isValid() && block.text().trimmed().isEmpty()) {
        block = block.next();
    }

    return block;
}

QString textBeforeCursor(QTextCursor cursor) {
    QString blockText = cursor.block().text();
    return blockText.left(cursor.positionInBlock());
}

void setPositionInBlock(QTextCursor *cursor, int positionInBlock, QTextCursor::MoveMode anchor) {
    return cursor->setPosition(cursor->block().position() + positionInBlock, anchor);
}

TextPosition findClosingBracketForward(QChar bracket, const TextPosition &position) {
    QChar opening = bracket;
    QChar closing;

    if (bracket == '(') {
        closing = ')';
    } else if (bracket == '[') {
        closing = ']';
    } else if (bracket == '{') {
        closing = '}';
    } else {
        qDebug() << "Invalid bracket" << bracket;
        return TextPosition();
    }

    int depth = 1;

    ForwardCharIterator it(position);
    it.step();

    while (!it.atEnd()) {
        QChar ch = it.step();
        // TODO if not self._qpart.isComment(foundBlock.blockNumber(),
        // foundColumn):
        if (ch == opening) {
            depth++;
        } else if (ch == closing) {
            depth--;
        }

        if (depth == 0) {
            return it.previousPosition();
        }
    }

    return TextPosition();
}

TextPosition findOpeningBracketBackward(QChar bracket, const TextPosition &position) {
    QChar opening = QChar::Null;
    QChar closing = QChar::Null;

    if (bracket == '(' || bracket == ')') {
        opening = '(';
        closing = ')';
    } else if (bracket == '[' || bracket == ']') {
        opening = '[';
        closing = ']';
    } else if (bracket == '{' || bracket == '}') {
        opening = '{';
        closing = '}';
    } else {
        qDebug() << "Invalid bracket" << bracket;
        return TextPosition();
    }

    int depth = 1;

    BackwardCharIterator it(position);
    it.step();
    while (!it.atEnd()) {
        QChar ch = it.step();
        // TODO if not self._qpart.isComment(foundBlock.blockNumber(),
        // foundColumn):
        if (ch == opening) {
            depth--;
        } else if (ch == closing) {
            depth++;
        } else if (ch == QChar('\0')) {
            return {};
        }

        if (depth == 0) {
            return it.previousPosition();
        }
    }

    return TextPosition();
}

TextPosition findAnyOpeningBracketBackward(const TextPosition &pos) {
    std::map<std::pair<QChar, QChar>, int> depth;

    depth[std::make_pair('(', ')')] = 1;
    depth[std::make_pair('[', ']')] = 1;
    depth[std::make_pair('{', '}')] = 1;

    BackwardCharIterator it(pos);
    it.step();

    while (!it.atEnd()) {
        QChar ch = it.step();
        // if self._qpart.isCode(foundBlock.blockNumber(), foundColumn):

        for (auto mapIt = depth.begin(); mapIt != depth.end(); ++mapIt) {
            QChar opening = mapIt->first.first;
            QChar closing = mapIt->first.second;

            if (ch == opening) {
                mapIt->second--;
                if (mapIt->second == 0) {
                    return it.previousPosition();
                }
            } else if (ch == closing) {
                mapIt->second++;
            }
        }
    }

    return TextPosition();
}

} // namespace Qutepart
