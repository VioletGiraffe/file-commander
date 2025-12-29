/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <QTextBlock>
#include <qutepart.h>

namespace Qutepart {

bool hasFlag(const QTextBlock &block, int flag);
void setFlag(QTextBlock &block, int flag, bool value);

inline bool isBookmarked(const QTextBlock &block) { return hasFlag(block, BOOMARK_BIT); }
inline void setBookmarked(QTextBlock &block, bool value) { setFlag(block, BOOMARK_BIT, value); }

} // namespace Qutepart
