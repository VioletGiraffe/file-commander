/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <QHash>
#include <QRegularExpression>
#include <QSharedPointer>
#include <QString>
#include <QTextStream>

#include "context.h"
#include "first_chars.h"
#include "text_to_match.h"

namespace Qutepart {

class Context;
typedef QSharedPointer<Context> ContextPtr;
class TextToMatch;
class Language;

struct AbstractRuleParams {
    QString attribute; // may be null
    ContextSwitcher context;
    bool lookAhead;
    bool firstNonSpace;
    int column; // -1 if not set
    bool dynamic;
    QString beginRegion;
    QString endRegion;
};

class AbstractRule {
  public:
    AbstractRule(const AbstractRuleParams &params);
    virtual ~AbstractRule() {}

    virtual void printDescription(QTextStream &out) const;
    virtual QString description() const;

    virtual void resolveContextReferences(const QHash<QString, ContextPtr> &contexts,
                                          QString &error);
    virtual void setKeywordParams(const QHash<QString, QStringList> &, bool, const QString &,
                                  QString &) {}
    void setStyles(const QHash<QString, Style> &styles, QString &error);
    void setTheme(const Theme *theme);

    bool lookAhead;
    QSharedPointer<Language> language;

    /* Characters this rule can start a match with. Contexts consult it before
     * calling tryMatch(), which is what keeps regexps and keyword lookups off
     * the hot path for the columns they could never match at anyway.
     * Recomputed by computeFirstChars() once the rule is fully configured.
     */
    FirstCharSet firstChars;

    /* Fills firstChars, once. `depth` bounds the recursion through IncludeRules. */
    void ensureFirstChars(int depth = 0);

    /* Matching entrypoint. Checks common params and calls tryMatchImpl()
     * Returns true and fills result on a match; result is untouched otherwise.
     */
    bool tryMatch(const TextToMatch &textToMatch, MatchResult &result) const;

  protected:
    friend class Context;
    virtual QString name() const { return "AbstractRule"; }
    virtual QString args() const { return QString(); }

    bool makeMatchResult(MatchResult &result, int length, bool lineContinue = false) const;

    /* Whether the context this rule switches to can read back the capture
     * groups. Extracting them costs a QStringList per match, so it is skipped
     * for the (vast majority of) rules whose target context has no way to use
     * them. Decided by finalize(), once every context reference is resolved.
     */
    bool capturesNeeded = false;

    /* Whether this rule itself substitutes capture groups into its pattern. */
    virtual bool readsCaptures() const { return dynamic; }

    void finalize();

  protected:
    virtual void computeFirstChars(int depth) {
        (void)depth;
        firstChars.setUnknown();
    }
    bool firstCharsReady = false;

    /* A rule flagged dynamic whose pattern has no %0..%4 placeholder would
     * substitute nothing into it. Dropping the flag lets it be compiled once
     * and lets its first characters be known - dynamic patterns are otherwise
     * rebuilt, recompiled and JIT-compiled on every single match attempt.
     */
    void dropDynamicWithoutPlaceholders(const QString &pattern);

  public:
    /* Rule matching implementation
     * Returns true and fills result on a match; result is untouched otherwise.
     */
    virtual bool tryMatchImpl(const TextToMatch &textToMatch, MatchResult &result) const = 0;

    QString attribute; // may be null
    ContextSwitcher contextSwitcher;
    bool firstNonSpace;
    int column; // -1 if not set
    bool dynamic;
    Style style;
    QString beginRegion;
    QString endRegion;
};

// A rule which has 1 string as a parameter
class AbstractStringRule : public AbstractRule {
  public:
    AbstractStringRule(const AbstractRuleParams &params, const QString &value, bool insensitive);

  protected:
    QString args() const override;
    QString value;
    bool insensitive;
};

class KeywordRule : public AbstractRule {
  public:
    KeywordRule(const AbstractRuleParams &params, const QString &listName);

    void setKeywordParams(const QHash<QString, QStringList> &lists, bool caseSensitive,
                          const QString &, QString &error) override;

    QString name() const override { return "Keyword"; }
    QString args() const override { return listName; }
    void computeFirstChars(int depth) override;

  private:
    virtual bool tryMatchImpl(const TextToMatch &textToMatch, MatchResult &result) const override;

    QString listName;
    QHash<QString, bool> items;
    bool caseSensitive;
    const DeliminatorSet *deliminators = sharedDeliminatorSet(QString());
};

class DetectCharRule : public AbstractRule {
  public:
    DetectCharRule(const AbstractRuleParams &params, QChar value, int index);

    QString name() const override { return "DetectChar"; }
    QString args() const override;
    void computeFirstChars(int depth) override;

  private:
    virtual bool tryMatchImpl(const TextToMatch &textToMatch, MatchResult &result) const override;

    QChar value;
    int index;
};

class Detect2CharsRule : public AbstractStringRule {
    using AbstractStringRule::AbstractStringRule;

  public:
    QString name() const override { return "Detect2Chars"; }
    void computeFirstChars(int depth) override;

  private:
    bool tryMatchImpl(const TextToMatch &textToMatch, MatchResult &result) const override;
};

class AnyCharRule : public AbstractStringRule {
    using AbstractStringRule::AbstractStringRule;

  public:
    QString name() const override { return "AnyChar"; }
    void computeFirstChars(int depth) override;

  private:
    virtual bool tryMatchImpl(const TextToMatch &textToMatch, MatchResult &result) const override;
};

class StringDetectRule : public AbstractStringRule {
    using AbstractStringRule::AbstractStringRule;

  public:
    QString name() const override { return "StringDetect"; }
    void computeFirstChars(int depth) override;

  private:
    virtual bool tryMatchImpl(const TextToMatch &textToMatch, MatchResult &result) const override;
};

class WordDetectRule : public AbstractStringRule {
    using AbstractStringRule::AbstractStringRule;

  public:
    QString name() const override { return "WordDetect"; }
    void computeFirstChars(int depth) override;
    void setKeywordParams(const QHash<QString, QStringList> &lists, bool caseSensitive,
                          const QString &, QString &error) override;

  private:
    virtual bool tryMatchImpl(const TextToMatch &textToMatch, MatchResult &result) const override;
    const DeliminatorSet *mDeliminatorSet = sharedDeliminatorSet(QString());
};

class RegExpRule : public AbstractRule {
  public:
    RegExpRule(const AbstractRuleParams &params, const QString &value, bool insensitive,
               bool minimal, bool wordStart, bool lineStart);
    void computeFirstChars(int depth) override;

  private:
    QString name() const override { return "RegExpr"; }
    QString args() const override;

    QRegularExpression compileRegExp(const QString &pattern) const;
    virtual bool tryMatchImpl(const TextToMatch &textToMatch, MatchResult &result) const override;

    QString value;
    bool insensitive;
    bool minimal;
    bool wordStart;
    bool lineStart;
    QRegularExpression regExp;

    /* Memo for the still-dynamic patterns: the substituted pattern only
     * changes when the enclosing context's captures do, which is rare compared
     * to how often the rule is tried. */
    mutable QString dynamicPattern;
    mutable QRegularExpression dynamicRegExp;
};

class AbstractNumberRule : public AbstractRule {
  public:
    AbstractNumberRule(const AbstractRuleParams &params, const QList<RulePtr> &childRules);

    void printDescription(QTextStream &out) const override;

  protected:
    bool tryMatchImpl(const TextToMatch &textToMatch, MatchResult &result) const override;
    virtual int tryMatchText(const QStringView &text) const = 0;
    int countDigits(const QStringView &text) const;

    QList<RulePtr> childRules;
};

class IntRule : public AbstractNumberRule {
    using AbstractNumberRule::AbstractNumberRule;

  public:
    QString name() const override { return "Int"; }
    void computeFirstChars(int depth) override;

  private:
    virtual int tryMatchText(const QStringView &text) const override;
};

class FloatRule : public AbstractNumberRule {
    using AbstractNumberRule::AbstractNumberRule;

  public:
    QString name() const override { return "Float"; }
    void computeFirstChars(int depth) override;

  private:
    int tryMatchText(const QStringView &text) const override;
};

class HlCOctRule : public AbstractRule {
    using AbstractRule::AbstractRule;

  public:
    QString name() const override { return "HlCOct"; }
    void computeFirstChars(int depth) override;

  private:
    bool tryMatchImpl(const TextToMatch &textToMatch, MatchResult &result) const override;
};

class HlCHexRule : public AbstractRule {
    using AbstractRule::AbstractRule;

  public:
    QString name() const override { return "HlCHex"; }
    void computeFirstChars(int depth) override;

  private:
    bool tryMatchImpl(const TextToMatch &textToMatch, MatchResult &result) const override;
};

class HlCStringCharRule : public AbstractRule {
    using AbstractRule::AbstractRule;

  public:
    QString name() const override { return "HlCStringChar"; }
    void computeFirstChars(int depth) override;

  private:
    bool tryMatchImpl(const TextToMatch &textToMatch, MatchResult &result) const override;
};

class HlCCharRule : public AbstractRule {
    using AbstractRule::AbstractRule;

  public:
    QString name() const override { return "HlCChar"; }
    void computeFirstChars(int depth) override;

  private:
    bool tryMatchImpl(const TextToMatch &textToMatch, MatchResult &result) const override;
};

class RangeDetectRule : public AbstractRule {
  public:
    RangeDetectRule(const AbstractRuleParams &params, const QString &char0, const QString &char1);
    QString name() const override { return "RangeDetect"; }
    QString args() const override;
    void computeFirstChars(int depth) override;

  private:
    bool tryMatchImpl(const TextToMatch &textToMatch, MatchResult &result) const override;

    const QString char0;
    const QString char1;
};

class LineContinueRule : public AbstractRule {
    using AbstractRule::AbstractRule;

  public:
    QString name() const override { return "LineContinue"; }
    void computeFirstChars(int depth) override;

  private:
    bool tryMatchImpl(const TextToMatch &textToMatch, MatchResult &result) const override;
};

class IncludeRulesRule : public AbstractRule {
  public:
    IncludeRulesRule(const AbstractRuleParams &params, const QString &contextName);

    QString name() const override { return "IncludeRules"; }
    QString args() const override { return contextName; }
    bool readsCaptures() const override;
    void computeFirstChars(int depth) override;

    void resolveContextReferences(const QHash<QString, ContextPtr> &contexts,
                                  QString &error) override;

  private:
    bool tryMatchImpl(const TextToMatch &textToMatch, MatchResult &result) const override;

    QString contextName;
    ContextPtr context;
};

class DetectSpacesRule : public AbstractRule {
    using AbstractRule::AbstractRule;

  public:
    QString name() const override { return "DetectSpaces"; }
    void computeFirstChars(int depth) override;

  private:
    bool tryMatchImpl(const TextToMatch &textToMatch, MatchResult &result) const override;
};

class DetectIdentifierRule : public AbstractRule {
    using AbstractRule::AbstractRule;

  public:
    QString name() const override { return "DetectIdentifier"; }
    void computeFirstChars(int depth) override;

  private:
    bool tryMatchImpl(const TextToMatch &textToMatch, MatchResult &result) const override;
};

} // namespace Qutepart
