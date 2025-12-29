/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <QTextBlock>

namespace Qutepart {

// Check if text at given position is a code
bool isCode(const QTextBlock &block, int column);

// Check if text at given position is a comment. Including block comments and
// here documents
bool isComment(const QTextBlock &block, int column);

// Check if text at given position is a block comment
bool isBlockComment(const QTextBlock &block, int column);

// Check if text at given position is a here document
bool isHereDoc(const QTextBlock &block, int column);

QString textTypeMap(const QTextBlock &block);

} // namespace Qutepart
