/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <QList>
#include <QSet>
#include <QTextBlock>
#include <QTextStream>

#include "context.h"
#include "context_stack.h"

namespace Qutepart {

class Theme;

class Language {
  public:
    Language(const QString &name, const QStringList &extensions, const QStringList &mimetypes,
             int priority, bool hidden, const QString &indenter, QString startMultilineComment,
             QString endMultilineComment, QString singleLineComment,
             const QSet<QString> &allLanguageKeywords, const QList<ContextPtr> &contexts);

    void printDescription(QTextStream &out) const;

    /* Highlights one block and returns the state to store on it.
     * `text` must be the block's own text; the overload below fetches it, which
     * costs a document lookup callers that already have the text can skip.
     */
    int highlightBlock(QTextBlock block, const QString &text,
                       QVector<QTextLayout::FormatRange> &formats);
    int highlightBlock(QTextBlock block, QVector<QTextLayout::FormatRange> &formats);

    inline ContextPtr defaultContext() const { return contexts.first(); }
    ContextPtr getContext(const QString &contextName) const;
    void setTheme(const Theme *theme);

    inline const QSet<QString> &allLanguageKeywords() const { return allLanguageKeywords_; }
    inline const QString &getStartMultilineComment() const { return startMultilineComment; }
    inline const QString &getEndMultilineComment() const { return endMultilineComment; }
    inline const QString &getSingleLineComment() const { return singleLineComment; }
    inline const QString &getName() const { return name; }

    QString fileName;

  protected:
    QString name;
    QString startMultilineComment;
    QString endMultilineComment;
    QString singleLineComment;

    QStringList extensions;
    QStringList mimetypes;
    int priority;
    bool hidden;
    QString indenter;
    QSet<QString> allLanguageKeywords_;

    QList<ContextPtr> contexts;
    ContextStack defaultContextStack;

    ContextStack getContextStack(QTextBlock block);
    void switchAtEndOfLine(ContextStack &contextStack);
};

} // namespace Qutepart
