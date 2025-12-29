/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <QHash>
#include <QSharedPointer>
#include <QTextLayout>
#include <QTextStream>

#include "context_stack.h"
#include "context_switcher.h"
#include "style.h"

namespace Qutepart {

class Context;
typedef QSharedPointer<Context> ContextPtr;

class AbstractRule;
typedef QSharedPointer<AbstractRule> RulePtr;

class Language;
class TextToMatch;
class MatchResult;
class Theme;
class TextBlockUserData;

class Context {
  public:
    Context(const QString &name, const QString &attribute, const ContextSwitcher &lineEndContext,
            const ContextSwitcher &lineBeginContext, const ContextSwitcher &lineEmptyContext,
            const ContextSwitcher &fallthroughContext, bool dynamic, const QList<RulePtr> &rules);

    void printDescription(QTextStream &out) const;

    QString name() const;

    void setTheme(const Theme *theme);
    void resolveContextReferences(const QHash<QString, ContextPtr> &contexts, QString &error);
    void setKeywordParams(const QHash<QString, QStringList> &lists, const QString &deliminators,
                          bool caseSensitive, QString &error);
    void setStyles(const QHash<QString, Style> &styles, QString &error);

    inline bool dynamic() const { return _dynamic; }
    inline ContextSwitcher lineBeginContext() const { return _lineBeginContext; }
    inline ContextSwitcher lineEndContext() const { return _lineEndContext; }

    const ContextStack parseBlock(const ContextStack &contextStack, TextToMatch &textToMatch,
                                  QVector<QTextLayout::FormatRange> &formats, QString &textTypeMap,
                                  bool &lineContinue, TextBlockUserData *data) const;

    // Try to match textToMatch with nested rules
    MatchResult *tryMatch(const TextToMatch &textToMatch) const;

    QSharedPointer<Language> language;

  protected:
    void applyMatchResult(const TextToMatch &textToMatch, const MatchResult *matchRes,
                          const Context *context, QVector<QTextLayout::FormatRange> &formats,
                          QString &textTypeMap) const;

    QString _name;
    QString attribute;
    const Theme *theme = nullptr;
    ContextSwitcher _lineEndContext;
    ContextSwitcher _lineBeginContext;
    ContextSwitcher _lineEmptyContext;
    ContextSwitcher fallthroughContext;
    bool _dynamic;
    QList<RulePtr> rules;
    Style style;
};

} // namespace Qutepart
