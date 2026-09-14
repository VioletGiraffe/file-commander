/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "hl/loader.h"
#include "hl/syntax_highlighter.h"
#include "qutepart.h"

#include "hl_factory.h"

namespace Qutepart {

class Theme;

QSyntaxHighlighter *makeHighlighter(QObject *parent, const QString &languageId) {
    QSharedPointer<Language> language = loadLanguage(languageId);
    if (!language.isNull()) {
        return new SyntaxHighlighter(parent, language);
    }

    return nullptr;
}

QSyntaxHighlighter *makeHighlighter(QTextDocument *parent, const QString &languageId) {
    QSharedPointer<Language> language = loadLanguage(languageId);
    if (!language.isNull()) {
        return new SyntaxHighlighter(parent, language);
    }

    return nullptr;
}

} // namespace Qutepart
