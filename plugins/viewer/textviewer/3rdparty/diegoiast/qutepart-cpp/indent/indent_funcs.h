/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <QString>
#include <QTextBlock>

namespace Qutepart {

QString lineIndent(const QString &line);
QString blockIndent(QTextBlock block);
void setBlockIndent(QTextCursor *cursor, const QString &indent);
QString prevBlockIndent(QTextBlock block);

QString increaseIndent(const QString &line, const QString &indent);
QString decreaseIndent(const QString &line, const QString &indent);

QString makeIndent(int confWidth, bool confUseTabs);

/* Make indent text with specified with.
 * Contains width count of spaces, or tabs and spaces
 */
QString makeIndentFromWidth(int width, int confWidth, bool confUseTabs);

/* Make indent equal to column indent.
Shiftted by offset
*/
QString makeIndentAsColumn(QTextBlock block, int column, int confIndentWidth, bool confUseTabs,
                           int offset = 0);

QString prevNonEmptyBlockIndent(const QTextBlock &block);

// Block text with comments replaced with space
QString textWithCommentsWiped(const QTextBlock &block);

QChar firstNonSpaceChar(const QTextBlock &block);
QChar lastNonSpaceChar(const QTextBlock &block);

} // namespace Qutepart
