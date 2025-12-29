/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <QList>
#include <QPlainTextEdit>

#include "text_pos.h"

namespace Qutepart {

class Qutepart;

class BracketHighlighter {
  public:
    BracketHighlighter(Qutepart *q);
    ~BracketHighlighter() = default;

    QList<QTextEdit::ExtraSelection> extraSelections(const TextPosition &pos);
    QTextEdit::ExtraSelection makeMatchSelection(const TextPosition &pos, bool matched);

    inline const TextPosition getCachedMatch(TextPosition &pos) {
        if (pos == cachedBracket_) {
            return cachedMatchingBracket_;
        }
        return {};
    }

  private:
    QList<QTextEdit::ExtraSelection> highlightBracket(QChar bracket, const TextPosition &pos);
    TextPosition cachedBracket_;
    TextPosition cachedMatchingBracket_;

    Qutepart *qpart;
};

} // namespace Qutepart
