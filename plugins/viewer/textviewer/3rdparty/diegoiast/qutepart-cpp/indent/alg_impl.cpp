/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "indent_funcs.h"
#include "text_block_utils.h"

#include "alg_impl.h"

namespace Qutepart {

IndentAlgImpl::~IndentAlgImpl() {}

void IndentAlgImpl::setConfig(int width, bool useTabs) {
    width_ = width;
    useTabs_ = useTabs;
}

void IndentAlgImpl::setLanguage(const QString &language) { language_ = language; }

QString IndentAlgImpl::autoFormatLine(QTextBlock block) const {
    return computeSmartIndent(block, -1) + stripLeftWhitespace(block.text());
}

QString IndentAlgImpl::indentLine(QTextBlock block, int cursorPos) const {
    QString indent = computeSmartIndent(block, cursorPos);
    if (!indent.isNull()) {
        return indent + stripLeftWhitespace(block.text());
    } else {
        return block.text();
    }
}

QString IndentAlgImpl::computeSmartIndent(QTextBlock /*block*/, int /*cursorPos*/) const {
    return "";
}

const QString &IndentAlgImpl::triggerCharacters() const {
    static const QString NULL_STRING = QString();
    return NULL_STRING;
}

QString IndentAlgImpl::indentText() const { return makeIndent(width_, useTabs_); }

} // namespace Qutepart
