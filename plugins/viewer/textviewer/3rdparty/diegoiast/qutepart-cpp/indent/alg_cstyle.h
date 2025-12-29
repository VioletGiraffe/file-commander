/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <QString>
#include <QTextBlock>

#include "text_block_utils.h"

#include "indenter.h"

namespace Qutepart {

class IndentAlgCstyle : public IndentAlgImpl {
  public:
    const QString &triggerCharacters() const override;
    QString indentLine(QTextBlock block, int cursorPos) const override;
    QString computeSmartIndent(QTextBlock block, int cursorPos) const override;

  private:
    QString findLeftBrace(const QTextBlock &block, int column) const;
    TextPosition tryParenthesisBeforeBrace(const TextPosition &pos) const;
    QString trySwitchStatement(const QTextBlock &block) const;
    QString tryAccessModifiers(const QTextBlock &block) const;
    QString tryCComment(const QTextBlock &block) const;
    QString tryCppComment(const QTextBlock &block) const;
    QString tryBrace(const QTextBlock &block) const;
    QString tryCKeywords(const QTextBlock &block, bool isBrace) const;
    QString tryCondition(const QTextBlock &block) const;
    QString tryStatement(const QTextBlock &block) const;
    QString tryMatchedAnchor(const QTextBlock &block, bool autoIndent) const;
    QString indentLine(const QTextBlock &block, bool autoIndent) const;
    QString processChar(const QTextBlock &block, QChar c, int cursorPos) const;
};

} // namespace Qutepart
