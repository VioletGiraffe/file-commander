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
#include "first_chars.h"
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
    void setLanguage(QSharedPointer<Language> newLanguage);
    void resolveContextReferences(const QHash<QString, ContextPtr> &contexts, QString &error);
    void setKeywordParams(const QHash<QString, QStringList> &lists, const QString &deliminators,
                          bool caseSensitive, QString &error);
    void setStyles(const QHash<QString, Style> &styles, QString &error);

    inline bool dynamic() const { return _dynamic; }

    /* Whether anything running with this context on top of the stack can read
     * back the capture groups of the match that switched into it. */
    bool usesCaptures() const;

    /* Called once every context reference of the language is resolved. */
    void finalize();

    /* The characters any of this context's rules can start a match with, so
     * that an IncludeRules pointing here inherits the same knowledge. */
    const FirstCharSet &firstCharsOfRules(int depth) const;
    inline ContextSwitcher lineBeginContext() const { return _lineBeginContext; }
    inline ContextSwitcher lineEndContext() const { return _lineEndContext; }

    /* Parses from the current position until the end of the text or until the
     * context switches; `contextStack` is updated in place. */
    void parseBlock(ContextStack &contextStack, TextToMatch &textToMatch,
                    QVector<QTextLayout::FormatRange> &formats, QString &textTypeMap,
                    QVector<Language *> &languageMap, bool &lineContinue,
                    TextBlockUserData *data) const;

    // Try to match textToMatch with nested rules
    // Returns true and fills result on a match; result is untouched otherwise.
    bool tryMatch(const TextToMatch &textToMatch, MatchResult &result) const;

    QSharedPointer<Language> language;

  protected:
    void applyMatchResult(const TextToMatch &textToMatch, const MatchResult &matchRes,
                          const Context *context, QVector<QTextLayout::FormatRange> &formats,
                          QString &textTypeMap, QVector<Language *> &languageMap) const;

    QString _name;
    QString attribute;
    ContextSwitcher _lineEndContext;
    ContextSwitcher _lineBeginContext;
    ContextSwitcher _lineEmptyContext;
    ContextSwitcher fallthroughContext;
    bool _dynamic;
    QList<RulePtr> rules;
    Style style;

  private:
    /* Rules bucketed by the character they can start matching at.
     *
     * tryMatch() runs once per column of every line, and a context can hold
     * dozens of rules, nearly all of which cannot possibly match the character
     * under the cursor. Looking the candidates up by that character turns the
     * inner loop from "walk every rule" into "walk the two or three that stand
     * a chance".
     *
     * `candidates` holds the buckets back to back; bucket `c` spans
     * [bucketStart[c], bucketStart[c + 1]). Characters outside ASCII share
     * `nonAsciiCandidates`.
     */
    QVector<AbstractRule *> candidates;
    QVector<AbstractRule *> nonAsciiCandidates;
    quint32 bucketStart[129] = {};
    bool ruleIndexBuilt = false;

    /* -1 while undecided; usesCaptures() memoises the answer here. */
    mutable signed char capturesUsed = -1;

    mutable FirstCharSet ruleFirstChars;
    mutable bool ruleFirstCharsReady = false;
    mutable bool ruleFirstCharsBusy = false;

    void buildRuleIndex();
};

} // namespace Qutepart
