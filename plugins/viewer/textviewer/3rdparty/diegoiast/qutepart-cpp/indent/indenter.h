/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <QKeyEvent>
#include <QObject>
#include <QString>
#include <QTextBlock>

#include "alg_impl.h"
#include "qutepart.h"

namespace Qutepart {

class Indenter : public QObject {
  public:
    Indenter(QObject *parent);
    ~Indenter();

    void setAlgorithm(IndentAlg alg);

    QString indentText() const;

    int width() const;
    void setWidth(int);

    bool useTabs() const;
    void setUseTabs(bool);

    void setLanguage(const QString &language);

    bool shouldAutoIndentOnEvent(QKeyEvent *event) const;
    bool shouldUnindentWithBackspace(const QTextCursor &cursor) const;
#if 0
    void autoIndentBlock(QTextBlock block, QChar typedKey) const;
#endif
    void indentBlock(QTextBlock block, int cursorPos, int typedKey) const;
  public slots:
    void onShortcutIndentAfterCursor(QTextCursor cursor) const;
    void onShortcutUnindentWithBackspace(QTextCursor &cursor) const;

  private:
    IndentAlgImpl *alg_;
    bool useTabs_;
    int width_;
    QString language_;
};

} // namespace Qutepart
