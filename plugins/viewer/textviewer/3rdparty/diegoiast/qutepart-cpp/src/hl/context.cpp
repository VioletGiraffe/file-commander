/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include <QDebug>
#include <QScopedPointer>

#include "context.h"
#include "match_result.h"
#include "rules.h"
#include "text_block_user_data.h"
#include "text_to_match.h"
#include "theme.h"

namespace Qutepart {

Context::Context(const QString &name, const QString &attribute,
                 const ContextSwitcher &lineEndContext, const ContextSwitcher &lineBeginContext,
                 const ContextSwitcher &lineEmptyContext, const ContextSwitcher &fallthroughContext,
                 bool dynamic, const QList<RulePtr> &rules)
    : _name(name), attribute(attribute), _lineEndContext(lineEndContext),
      _lineBeginContext(lineBeginContext), _lineEmptyContext(lineEmptyContext),
      fallthroughContext(fallthroughContext), _dynamic(dynamic), rules(rules) {}

void Context::printDescription(QTextStream &out) const {
    out << "\tContext " << this->_name << "\n";
    out << "\t\tattribute: " << attribute << "\n";
    if (!_lineEndContext.isNull()) {
        out << "\t\tlineEndContext: " << _lineEndContext.toString() << "\n";
    }
    if (!_lineBeginContext.isNull()) {
        out << "\t\tlineBeginContext: " << _lineBeginContext.toString() << "\n";
    }
    if (!_lineEmptyContext.isNull()) {
        out << "\t\tlineEmptyContext: " << _lineEmptyContext.toString() << "\n";
    }
    if (!fallthroughContext.isNull()) {
        out << "\t\tfallthroughContext: " << fallthroughContext.toString() << "\n";
    }
    if (_dynamic) {
        out << "\t\tdynamic\n";
    }

    for (const auto &rule : std::as_const(rules)) {
        rule->printDescription(out);
    }
}

QString Context::name() const { return _name; }

void Context::setTheme(const Theme *theme) {
    style.setTheme(theme);

    if (_lineEndContext.context()) {
        _lineEndContext.context()->setTheme(theme);
    }
    if (_lineBeginContext.context()) {
        _lineBeginContext.context()->setTheme(theme);
    }
    if (_lineEmptyContext.context()) {
        _lineEmptyContext.context()->setTheme(theme);
    }
    if (fallthroughContext.context()) {
        fallthroughContext.context()->setTheme(theme);
    }

    for (auto &rule : rules) {
        rule->setTheme(theme);
    }
}

void Context::setLanguage(QSharedPointer<Language> newLanguage) {
    this->language = newLanguage;
    for (auto &rule : rules) {
        rule->language = newLanguage;
    }
}

void Context::resolveContextReferences(const QHash<QString, ContextPtr> &contexts, QString &error) {
    _lineEndContext.resolveContextReferences(contexts, error);
    if (!error.isNull()) {
        return;
    }

    _lineBeginContext.resolveContextReferences(contexts, error);
    if (!error.isNull()) {
        return;
    }

    _lineEmptyContext.resolveContextReferences(contexts, error);
    if (!error.isNull()) {
        return;
    }

    fallthroughContext.resolveContextReferences(contexts, error);
    if (!error.isNull()) {
        return;
    }

    for (auto &rule : rules) {
        rule->resolveContextReferences(contexts, error);
        if (!error.isNull()) {
            return;
        }
    }
}

void Context::setKeywordParams(const QHash<QString, QStringList> &lists,
                               const QString &deliminatorSet, bool caseSensitive, QString &error) {
    for (auto &rule : rules) {
        rule->setKeywordParams(lists, caseSensitive, deliminatorSet, error);
        if (!error.isNull()) {
            return;
        }
    }

}

bool Context::usesCaptures() const {
    if (capturesUsed >= 0) {
        return capturesUsed != 0;
    }

    // Assume "yes" while recursing, so that a cycle of mutually including
    // contexts errs towards keeping the captures rather than dropping them.
    capturesUsed = 1;

    if (_dynamic) {
        return true;
    }
    for (auto const &rule : rules) {
        if (rule->readsCaptures()) {
            return true;
        }
    }

    capturesUsed = 0;
    return false;
}

void Context::finalize() {
    for (auto &rule : rules) {
        rule->finalize();
    }
    buildRuleIndex();
}

const FirstCharSet &Context::firstCharsOfRules(int depth) const {
    static constexpr auto MaxIncludeDepth = 8;

    if (ruleFirstCharsReady) {
        return ruleFirstChars;
    }

    if (ruleFirstCharsBusy || depth > MaxIncludeDepth) {
        // Mutually including contexts, or an include chain too deep to be worth
        // following: claim everything, which only costs extra match attempts.
        ruleFirstChars.setUnknown();
        return ruleFirstChars;
    }

    ruleFirstCharsBusy = true;
    FirstCharSet united;
    united.clear();
    for (auto const &rule : rules) {
        rule->ensureFirstChars(depth + 1);
        united.unite(rule->firstChars);
        if (united.isUnknown()) {
            break;
        }
    }
    ruleFirstCharsBusy = false;

    ruleFirstChars = united;
    ruleFirstCharsReady = true;
    return ruleFirstChars;
}

void Context::buildRuleIndex() {
    for (auto &rule : rules) {
        rule->ensureFirstChars();
    }

    candidates.clear();
    nonAsciiCandidates.clear();
    for (auto code = 0; code < 128; code++) {
        bucketStart[code] = quint32(candidates.size());
        for (auto &rule : rules) {
            if (rule->firstChars.mayMatch(QChar(code))) {
                candidates.append(rule.data());
            }
        }
    }
    bucketStart[128] = quint32(candidates.size());

    for (auto &rule : rules) {
        if (rule->firstChars.isNonAscii()) {
            nonAsciiCandidates.append(rule.data());
        }
    }

    candidates.squeeze();
    nonAsciiCandidates.squeeze();
    ruleIndexBuilt = true;
}

void Context::setStyles(const QHash<QString, Style> &styles, QString &error) {
    if (!attribute.isNull()) {
        if (!styles.contains(attribute)) {
            error = QString("Not found context '%1' attribute '%2'").arg(_name, attribute);
            return;
        }
        style = styles[attribute];
        style.updateTextType(attribute);
    }

    for (auto &rule : rules) {
        rule->setStyles(styles, error);
        if (!error.isNull()) {
            break;
        }
    }
}

void appendFormat(QVector<QTextLayout::FormatRange> &formats, int start, int length,
                  const QTextCharFormat &format) {

    if ((!formats.isEmpty()) && (formats.last().start + formats.last().length) == start &&
        formats.last().format == format) {
        formats.last().length += length;
    } else {
        QTextLayout::FormatRange fmtRange;
        fmtRange.start = start;
        fmtRange.length = length;
        fmtRange.format = format;
        formats.append(fmtRange);
    }
}

void fillTextTypeMap(QString &textTypeMap, int start, int length, QChar textType) {
    auto *chars = textTypeMap.data() + start;
    for (auto i = 0; i < length; i++) {
        chars[i] = textType;
    }
}

void fillLanguageMap(QVector<Language *> &languageMap, int start, int length, Language *language) {
    auto *entries = languageMap.data() + start;
    for (auto i = 0; i < length; i++) {
        entries[i] = language;
    }
}

// Helper function for parseBlock()
void Context::applyMatchResult(const TextToMatch &textToMatch, const MatchResult &matchRes,
                               const Context *context, QVector<QTextLayout::FormatRange> &formats,
                               QString &textTypeMap, QVector<Language *> &languageMap) const {
    auto const *displayFormat = matchRes.style->formatData();

    if (displayFormat == nullptr) {
        displayFormat = context->style.formatData();
    }

    if (displayFormat != nullptr) {
        appendFormat(formats, textToMatch.currentColumnIndex, matchRes.length, *displayFormat);
    }

    QChar textType = matchRes.style->textType();
    if (textType == 0) {
        textType = context->style.textType();
    }
    fillTextTypeMap(textTypeMap, textToMatch.currentColumnIndex, matchRes.length, textType);

    auto *lang = matchRes.rule->language.data();
    if (lang == nullptr) {
        lang = context->language.data();
    }
    fillLanguageMap(languageMap, textToMatch.currentColumnIndex, matchRes.length, lang);
}

// Parse block. Exits, when reached end of the text, or when context is switched
void Context::parseBlock(ContextStack &contextStack, TextToMatch &textToMatch,
                         QVector<QTextLayout::FormatRange> &formats, QString &textTypeMap,
                         QVector<Language *> &languageMap, bool &lineContinue,
                         TextBlockUserData *data) const {
    textToMatch.contextData = &contextStack.currentData();

    if (textToMatch.isEmpty() && (!_lineEmptyContext.isNull())) {
        contextStack.switchTo(_lineEmptyContext);
        return;
    }

    MatchResult matchRes;
    while (!textToMatch.isEmpty()) {
        bool matched = tryMatch(textToMatch, matchRes);

        if (matched) {
            lineContinue = matchRes.lineContinue;

            if (data && !matchRes.rule->beginRegion.isEmpty()) {
                data->regions.push(matchRes.rule->beginRegion);
            }

            if (data && !matchRes.rule->endRegion.isEmpty()) {
                if (!data->regions.isEmpty() && data->regions.top() == matchRes.rule->endRegion) {
                    data->regions.pop();
                }
            }

            if (data) {
                data->folding.level = data->regions.size();
            }

            if (matchRes.nextContext->isNull()) {
                applyMatchResult(textToMatch, matchRes, this, formats, textTypeMap, languageMap);
                textToMatch.shift(matchRes.length);
            } else {
                // Invalidates textToMatch.contextData; the next parseBlock() call
                // reassigns it before anything reads it again.
                contextStack.switchTo(*matchRes.nextContext, matchRes.data);

                applyMatchResult(textToMatch, matchRes, contextStack.currentContext(), formats,
                                 textTypeMap, languageMap);
                textToMatch.shift(matchRes.length);
                return;
            }
        } else {
            lineContinue = false;
            if (auto const *format = style.formatData()) {
                appendFormat(formats, textToMatch.currentColumnIndex, 1, *format);
            }
            textTypeMap[textToMatch.currentColumnIndex] = style.textType();
            languageMap[textToMatch.currentColumnIndex] = this->language.data();
            if (!this->fallthroughContext.isNull()) {
                contextStack.switchTo(this->fallthroughContext);
                return;
            }
            textToMatch.shiftOnce();
        }
    }
}

bool Context::tryMatch(const TextToMatch &textToMatch, MatchResult &result) const {
    if (Q_UNLIKELY(!ruleIndexBuilt)) {
        for (auto &rule : rules) {
            if (rule->tryMatch(textToMatch, result)) {
                return true;
            }
        }
        return false;
    }

    auto const code = textToMatch.text.at(0).unicode();
    AbstractRule *const *candidate = nullptr;
    AbstractRule *const *end = nullptr;
    if (code < 128) {
        candidate = candidates.constData() + bucketStart[code];
        end = candidates.constData() + bucketStart[code + 1];
    } else {
        candidate = nonAsciiCandidates.constData();
        end = candidate + nonAsciiCandidates.size();
    }

    for (; candidate != end; ++candidate) {
        if ((*candidate)->tryMatch(textToMatch, result)) {
            return true;
        }
    }

    return false;
}

} // namespace Qutepart
