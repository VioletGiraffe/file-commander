/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <QSyntaxHighlighter>

namespace Qutepart {

class Theme;

/**
 * Choose and load a highlighter.
 *
 * Set as much parameters at posiible to detect language correctly
 *
 * See QSyntaxHighlighter::QSyntaxHighlighter(..) documentation.
 */
QSyntaxHighlighter *makeHighlighter(QObject *parent, const QString &languageId);

QSyntaxHighlighter *makeHighlighter(QTextDocument *parent, const QString &langugeId);

} // namespace Qutepart
