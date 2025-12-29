/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include <QTextLayout>
#include <Qt>

#include "language.h"
#include "syntax_highlighter.h"
#include "theme.h"

namespace Qutepart {

SyntaxHighlighter::SyntaxHighlighter(QTextDocument *parent, QSharedPointer<Language> language)
    : QSyntaxHighlighter(parent), language(language) {}

SyntaxHighlighter::SyntaxHighlighter(QObject *parent, QSharedPointer<Language> language)
    : QSyntaxHighlighter(parent), language(language) {}

void SyntaxHighlighter::highlightBlock(const QString &) {
    QVector<QTextLayout::FormatRange> formats;

    auto state = language->highlightBlock(currentBlock(), formats);
    for (auto &range : std::as_const(formats)) {
        setFormat(range.start, range.length, range.format);
    }
    setCurrentBlockState(state);
}

} // namespace Qutepart
