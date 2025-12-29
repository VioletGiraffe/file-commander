/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "text_block_user_data.h"

#include "text_type.h"

namespace Qutepart {

namespace {

static inline TextBlockUserData *getData(const QTextBlock &block) {
    return static_cast<TextBlockUserData *>(block.userData());
}

QChar getTextType(const QTextBlock &block, int column) {
    TextBlockUserData *data = getData(block);
    if (data == nullptr) {
        return ' ';
    } else {
        // this may happen on empty files
        if (data->textTypeMap.size() < column) {
            return ' ';
        }
        return data->textTypeMap[column];
    }
}

} // namespace

QString textTypeMap(const QTextBlock &block) {
    const TextBlockUserData *data = getData(block);
    if (data == nullptr) {
        return QString().fill(' ', block.text().length());
    }

    return data->textTypeMap;
}

bool isCode(const QTextBlock &block, int column) { return getTextType(block, column) == ' '; }

bool isComment(const QTextBlock &block, int column) {
    QChar type = getTextType(block, column);

    return type == 'c' || type == 'b' || type == 'h';
}

bool isBlockComment(const QTextBlock &block, int column) {
    return getTextType(block, column) == 'b';
}

bool isHereDoc(const QTextBlock &block, int column) { return getTextType(block, column) == 'h'; }

} // namespace Qutepart
