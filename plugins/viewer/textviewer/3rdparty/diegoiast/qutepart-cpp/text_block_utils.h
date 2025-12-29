/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <QString>
#include <QTextBlock>

#include "text_pos.h"

namespace Qutepart {

int firstNonSpaceColumn(const QString &line);
int lastNonSpaceColumn(const QString &line);

// Calculate spaces ( ' ' but not other whitespace symbols ) at the end
int spaceAtEndCount(const QString &text);

QString stripLeftWhitespace(const QString &line);
QString stripRightWhitespace(const QString &line);

QTextBlock prevNonEmptyBlock(QTextBlock block);
QTextBlock nextNonEmptyBlock(QTextBlock block);

QString textBeforeCursor(QTextCursor cursor);

void setPositionInBlock(QTextCursor *cursor, int positionInBlock,
                        QTextCursor::MoveMode anchor = QTextCursor::MoveAnchor);

/* find bracket forward from position (not including position)
   Return invalid position if not found
   NOTE this function ignores comments
 */
TextPosition findClosingBracketForward(QChar bracket, const TextPosition &position);

/* find bracket backward from position (not including position)
   Return invalid position if not found
   NOTE this function ignores comments
 */
TextPosition findOpeningBracketBackward(QChar bracket, const TextPosition &position);

/* Search for opening bracket. Ignores balanced bracket pairs

    (a + (b + c)
    ^
    this bracket will be found

NOTE this methods ignores strings and comments
TODO currently it doesn't
 */
TextPosition findAnyOpeningBracketBackward(const TextPosition &pos);

} // namespace Qutepart
