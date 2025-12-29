/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include <QDebug>

#include "loader.h"
#include "match_result.h"
#include "text_to_match.h"

#include "rules.h"

namespace Qutepart {

AbstractRule::AbstractRule(const AbstractRuleParams &params)
    : lookAhead(params.lookAhead), attribute(params.attribute), contextSwitcher(params.context),
      firstNonSpace(params.firstNonSpace), column(params.column), dynamic(params.dynamic),
      beginRegion(params.beginRegion), endRegion(params.endRegion) {}

void AbstractRule::printDescription(QTextStream &out) const {
    out << "\t\t" << description() << "\n";
}

QString AbstractRule::description() const { return QString("%1(%2)").arg(name()).arg(args()); }

void AbstractRule::resolveContextReferences(const QHash<QString, ContextPtr> &contexts,
                                            QString &error) {
    contextSwitcher.resolveContextReferences(contexts, error);
}

void AbstractRule::setStyles(const QHash<QString, Style> &styles, QString &error) {
    if (!attribute.isNull()) {
        if (!styles.contains(attribute)) {
            error = QString("Not found rule %1 attribute %2").arg(description(), attribute);
            return;
        }
        style = styles[attribute];
        style.updateTextType(attribute);
    }
}

void AbstractRule::setTheme(const Theme *theme) {
    if (theme == style.getTheme()) {
        return;
    }
#if 0
    qDebug() << "    - rule " << this->name() << args();
#endif
    style.setTheme(theme);
    if (contextSwitcher.context()) {
        contextSwitcher.context()->setTheme(theme);
    }
}

MatchResult *AbstractRule::makeMatchResult(int length, bool lineContinue,
                                           const QStringList &data) const {
    // qDebug() << "\t\trule matched" << description() << length << "lookAhead"
    // << lookAhead;
    if (lookAhead) {
        length = 0;
    }

    return new MatchResult(length, data, lineContinue, contextSwitcher, style, this);
}

MatchResult *AbstractRule::tryMatch(const TextToMatch &textToMatch) const {
    if (column != -1 && column != textToMatch.currentColumnIndex) {
        return nullptr;
    }

    if (firstNonSpace && (not textToMatch.firstNonSpace)) {
        return nullptr;
    }

    return tryMatchImpl(textToMatch);
}

AbstractStringRule::AbstractStringRule(const AbstractRuleParams &params, const QString &value,
                                       bool insensitive)
    : AbstractRule(params), value(value), insensitive(insensitive) {}

QString AbstractStringRule::args() const {
    QString result = value;
    if (insensitive) {
        result += " insensitive";
    }

    return result;
}

namespace {
QString makeDynamicSubsctitutions(QString pattern, const QStringList &data) {
    return pattern.replace("%0", QRegularExpression::escape(data.value(0)))
        .replace("%1", QRegularExpression::escape(data.value(1)))
        .replace("%2", QRegularExpression::escape(data.value(2)))
        .replace("%3", QRegularExpression::escape(data.value(3)))
        .replace("%4", QRegularExpression::escape(data.value(4)));
}
} // namespace

MatchResult *StringDetectRule::tryMatchImpl(const TextToMatch &textToMatch) const {
    QString pattern = value;
    if (dynamic) {
        pattern = makeDynamicSubsctitutions(value, *textToMatch.contextData);
    }

    if (pattern.isEmpty()) {
        return nullptr;
    }

    if (textToMatch.text.startsWith(pattern)) {
        return makeMatchResult(pattern.length());
    }

    return nullptr;
}

KeywordRule::KeywordRule(const AbstractRuleParams &params, const QString &listName)
    : AbstractRule(params), listName(listName), caseSensitive(true) {}

void KeywordRule::setKeywordParams(const QHash<QString, QStringList> &lists, bool newCaseSensitive,
                                   const QString &newDeliminators, QString &error) {
    if (!lists.contains(listName)) {
        error = QString("List '%1' not found").arg(error);
        return;
    }
    items = lists[listName];
    this->caseSensitive = newCaseSensitive;
    this->deliminators = newDeliminators;

    if (!this->caseSensitive) {
        for (auto it = items.begin(); it != items.end(); it++) {
            *it = (*it).toLower();
        }
    }
}

MatchResult *KeywordRule::tryMatchImpl(const TextToMatch &textToMatch) const {
    QString word = textToMatch.word(deliminators);

    if (word.isEmpty()) {
        return nullptr;
    }

    bool matched = false;
    if (this->caseSensitive) {
        matched = items.contains(word);
    } else {
        matched = items.contains(word.toLower());
    }

    if (matched) {
        return makeMatchResult(word.length(), false);
    } else {
        return nullptr;
    }
}

DetectCharRule::DetectCharRule(const AbstractRuleParams &params, QChar value, int index)
    : AbstractRule(params), value(value), index(index) {}

QString DetectCharRule::args() const {
    if (value == QChar('\0')) {
        return QString("index: %1").arg(index);
    } else {
        return value;
    }
}

MatchResult *DetectCharRule::tryMatchImpl(const TextToMatch &textToMatch) const {
    QChar pattern = value;

    if (dynamic) {
        int matchIndex = this->index - 1;
        if (textToMatch.contextData == nullptr) {
            qWarning() << "Dynamic DetectCharRule but no data";
        }

        if (matchIndex >= textToMatch.contextData->length()) {
            qWarning() << "Invalid DetectChar index" << matchIndex;
            return nullptr;
        }

        if (textToMatch.contextData->at(matchIndex).length() != 1) {
            qWarning() << "Too long DetectChar string " << *textToMatch.contextData;
            return nullptr;
        }

        pattern = textToMatch.contextData->at(matchIndex)[0];
    }

    if (textToMatch.text.at(0) == pattern) {
        return makeMatchResult(1, false);
    } else {
        return nullptr;
    }
}

MatchResult *Detect2CharsRule::tryMatchImpl(const TextToMatch &textToMatch) const {
    if (textToMatch.text.startsWith(value)) {
        return makeMatchResult(2);
    }

    return nullptr;
}

MatchResult *AnyCharRule::tryMatchImpl(const TextToMatch &textToMatch) const {
    if (value.contains(textToMatch.text.at(0))) {
        return makeMatchResult(1);
    }

    return nullptr;
}

MatchResult *WordDetectRule::tryMatchImpl(const TextToMatch &textToMatch) const {
    QString word = textToMatch.word(mDeliminatorSet);
    if (word.isEmpty()) {
        return nullptr;
    }

    if (insensitive) {
        word = word.toLower();
    }

    if (word == value) {
        return makeMatchResult(word.length());
    } else {
        return nullptr;
    }
}

void WordDetectRule::setKeywordParams(const QHash<QString, QStringList> &, bool,
                                      const QString &deliminatorSet, QString &) {
    mDeliminatorSet = deliminatorSet;
}

RegExpRule::RegExpRule(const AbstractRuleParams &params, const QString &value, bool insensitive,
                       bool minimal, bool wordStart, bool lineStart)
    : AbstractRule(params), value(value), insensitive(insensitive), minimal(minimal),
      wordStart(wordStart), lineStart(lineStart) {
    if (!dynamic) {
        regExp = compileRegExp(value);
    }
}

QString RegExpRule::args() const {
    QString result = value;
    if (insensitive) {
        result += " insensitive";
    }
    if (minimal) {
        result += " minimal";
    }
    if (wordStart) {
        result += " wordStart";
    }
    if (lineStart) {
        result += " lineStart";
    }

    return result;
}

QRegularExpression RegExpRule::compileRegExp(const QString &pattern) const {
    QRegularExpression::PatternOptions flags = QRegularExpression::NoPatternOption;

    if (insensitive) {
        flags |= QRegularExpression::CaseInsensitiveOption;
    }

    if (minimal) {
        /* NOTE
         * There are support for minimal flag in Qt5 reg exps.
         * InvertedGreedinessOption would work only if reg exps are
         * written as non-greedy in the syntax files
         */
        flags |= QRegularExpression::InvertedGreedinessOption;
    }

    QRegularExpression result(pattern, flags);
    if (!result.isValid()) {
        qWarning() << "Invalid regular expression pattern" << pattern;
    }

    return result;
}

MatchResult *RegExpRule::tryMatchImpl(const TextToMatch &textToMatch) const {
    // Special case. if pattern starts with \b, we have to check it manually,
    // because string is passed to .match(..) without beginning
    if (wordStart && (!textToMatch.isWordStart)) {
        return nullptr;
    }

    // Special case. If pattern starts with ^ - check column number manually
    if (lineStart && textToMatch.currentColumnIndex > 0) {
        return nullptr;
    }

    QRegularExpressionMatch match;
    if (dynamic) {
        QString pattern = makeDynamicSubsctitutions(value, *textToMatch.contextData);
        QRegularExpression dynamicRegExp = compileRegExp(pattern);
        match = dynamicRegExp.matchView(textToMatch.text, 0, QRegularExpression::NormalMatch,
                                        QRegularExpression::AnchorAtOffsetMatchOption);
    } else {
        match = regExp.matchView(textToMatch.text, 0, QRegularExpression::NormalMatch,
                                 QRegularExpression::AnchorAtOffsetMatchOption);
    }

    if (match.hasMatch() && match.capturedLength() > 0) {
        return makeMatchResult(match.capturedLength(), false, match.capturedTexts());
    } else {
        return nullptr;
    }
}

AbstractNumberRule::AbstractNumberRule(const AbstractRuleParams &params,
                                       const QList<RulePtr> &childRules)
    : AbstractRule(params), childRules(childRules) {}

void AbstractNumberRule::printDescription(QTextStream &out) const {
    AbstractRule::printDescription(out);

    for (auto const &rule : std::as_const(childRules)) {
        out << "\t\t\t" << rule->description() << "\n";
    }
}

MatchResult *AbstractNumberRule::tryMatchImpl(const TextToMatch &textToMatch) const {
    // andreikop: This condition is not described in kate docs, and I haven't
    // found it in the code
    if (!textToMatch.isWordStart) {
        return nullptr;
    }

    int matchedLength = tryMatchText(textToMatch.text);

    if (matchedLength <= 0) {
        return nullptr;
    }

    if (matchedLength < textToMatch.text.length()) {
        TextToMatch textToMatchCopy = textToMatch;
        textToMatchCopy.shift(matchedLength);

        for (auto const &rule : std::as_const(childRules)) {
            MatchResult *matchRes = rule->tryMatch(textToMatchCopy);
            if (matchRes != nullptr) {
                matchedLength += matchRes->length;
                delete matchRes;
                break;
            }
            // child rule context and attribute ignored
        }
    }

    return makeMatchResult(matchedLength);
}

int AbstractNumberRule::countDigits(const QStringView &text) const {
    int index = 0;
    for (index = 0; index < text.length(); index++) {
        if (!text.at(index).isDigit()) {
            break;
        }
    }
    return index;
}

int IntRule::tryMatchText(const QStringView &text) const { return countDigits(text); }

int FloatRule::tryMatchText(const QStringView &text) const {
    bool haveDigit = false;
    bool havePoint = false;

    int matchedLength = 0;

    int digitCount = countDigits(text);

    if (digitCount > 0) {
        haveDigit = true;
        matchedLength += digitCount;
    }

    if (text.length() > matchedLength && text.at(matchedLength) == '.') {
        havePoint = true;
        matchedLength++;
    }

    digitCount = countDigits(text.mid(matchedLength));
    if (digitCount > 0) {
        haveDigit = true;
        matchedLength += digitCount;
    }

    if (text.length() > matchedLength && text.at(matchedLength).toLower() == 'e') {
        matchedLength++;

        if (text.length() > matchedLength &&
            (text.at(matchedLength) == '+' || text.at(matchedLength) == '-')) {
            matchedLength++;
        }

        bool haveDigitInExponent = false;

        digitCount = countDigits(text.mid(matchedLength));
        if (digitCount > 0) {
            haveDigitInExponent = true;
            matchedLength += digitCount;
        }

        if (!haveDigitInExponent) {
            return -1;
        }

        return matchedLength;
    } else {
        if (!havePoint) {
            return -1;
        }
    }

    if (matchedLength > 0 && haveDigit) {
        return matchedLength;
    } else {
        return -1;
    }
}

namespace { // HlC helpers

// For HlCOctRule and HlCHexRule
bool isNumberLengthSpecifier(QChar ch) { return ch == 'l' || ch == 'L' || ch == 'u' || ch == 'U'; }

bool isOctal(QChar ch) { return ch >= '0' && ch <= '7'; }

bool isHex(QChar ch) {
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

const QString escapeChars = "abefnrtv'\"?\\";

int checkEscapedChar(QStringView text) {
    int index = 0;
    if (text.length() > 1 && text.at(0) == '\\') {
        index = 1;

        if (escapeChars.contains(text.at(index))) {
            index++;
        } else if (text.at(index) == 'x') { // if it's like \xff, eat the x
            index++;
            while (index < text.length() && isHex(text.at(index))) {
                index++;
            }
            if (index == 2) { // no hex digits
                return -1;
            }
        } else if (isOctal(text.at(index))) {
            while (index < 4 && index < text.length() && isOctal(text.at(index))) {
                index++;
            }
        } else {
            return -1;
        }

        return index;
    }

    return -1;
}
} // namespace

MatchResult *HlCOctRule::tryMatchImpl(const TextToMatch &textToMatch) const {
    if (textToMatch.text.at(0) != '0') {
        return nullptr;
    }

    int index = 1;
    while (index < textToMatch.text.length() && isOctal(textToMatch.text.at(index))) {
        index++;
    }

    if (index == 1) {
        return nullptr;
    }

    if (index < textToMatch.text.length() && isNumberLengthSpecifier(textToMatch.text.at(index))) {
        index++;
    }

    return makeMatchResult(index);
}

MatchResult *HlCHexRule::tryMatchImpl(const TextToMatch &textToMatch) const {
    if (textToMatch.text.length() < 3) {
        return nullptr;
    }

    if (textToMatch.text.at(0) != '0' || textToMatch.text.at(1).toUpper() != 'X') {
        return nullptr;
    }

    int index = 2;
    while (index < textToMatch.text.length() && isHex(textToMatch.text.at(index))) {
        index++;
    }

    if (index == 2) {
        return nullptr;
    }

    if (index < textToMatch.text.length() && isNumberLengthSpecifier(textToMatch.text.at(index))) {
        index++;
    }

    return makeMatchResult(index);
}

MatchResult *HlCStringCharRule::tryMatchImpl(const TextToMatch &textToMatch) const {
    int res = checkEscapedChar(textToMatch.text);
    if (res != -1) {
        return makeMatchResult(res);
    } else {
        return nullptr;
    }
}

MatchResult *HlCCharRule::tryMatchImpl(const TextToMatch &textToMatch) const {
    if (textToMatch.text.length() > 2 && textToMatch.text.at(0) == '\'' &&
        textToMatch.text.at(1) != '\'') {
        int index = 0;
        int result = checkEscapedChar(textToMatch.text.mid(1));
        if (result != -1) {
            index = 1 + result;
        } else { // 1 not escaped character
            index = 1 + 1;
        }

        if (index < textToMatch.text.length() && textToMatch.text.at(index) == '\'') {
            return makeMatchResult(index + 1);
        }
    }

    return nullptr;
}

RangeDetectRule::RangeDetectRule(const AbstractRuleParams &params, const QString &char0,
                                 const QString &char1)
    : AbstractRule(params), char0(char0), char1(char1) {}

QString RangeDetectRule::args() const { return QString("%1 - %2").arg(char0, char1); }

MatchResult *RangeDetectRule::tryMatchImpl(const TextToMatch &textToMatch) const {
    if (textToMatch.text.startsWith(char0)) {
        int end = textToMatch.text.indexOf(char1, 1);
        if (end > 0) {
            return makeMatchResult(end + 1);
        }
    }

    return nullptr;
}

IncludeRulesRule::IncludeRulesRule(const AbstractRuleParams &params, const QString &contextName)
    : AbstractRule(params), contextName(contextName) {}

void IncludeRulesRule::resolveContextReferences(const QHash<QString, ContextPtr> &contexts,
                                                QString &error) {
    AbstractRule::resolveContextReferences(contexts, error);
    if (!error.isNull()) {
        return;
    }

    if (contextName.contains("#")) {
        context = loadExternalContext(contextName);
        if (context.isNull()) {
            error = QString("Failed to include rules from external context '%1'").arg(contextName);
            return;
        }
        return;
    }

    if (!contexts.contains(contextName)) {
        error = QString("Failed to include rules from context '%1' because not exists")
                    .arg(contextName);
        return;
    }

    context = contexts[contextName];
}

MatchResult *IncludeRulesRule::tryMatchImpl(const TextToMatch &textToMatch) const {
    if (context == nullptr) {
        qWarning() << "IncludeRules called for null context" << description();
        return nullptr;
    }

    return context->tryMatch(textToMatch);
}

MatchResult *LineContinueRule::tryMatchImpl(const TextToMatch &textToMatch) const {
    if (textToMatch.text == QLatin1String("\\")) {
        return makeMatchResult(1, true);
    }

    return nullptr;
}

MatchResult *DetectSpacesRule::tryMatchImpl(const TextToMatch &textToMatch) const {
    int index = 0;
    while (index < textToMatch.text.length() && textToMatch.text.at(index).isSpace()) {
        index++;
    }

    if (index > 0) {
        return makeMatchResult(index);
    } else {
        return nullptr;
    }
}

MatchResult *DetectIdentifierRule::tryMatchImpl(const TextToMatch &textToMatch) const {
    if (textToMatch.text.at(0).isLetter()) {
        int count = 1;
        while (count < textToMatch.text.length()) {
            QChar ch = textToMatch.text.at(count);
            if (ch.isLetterOrNumber() || ch == '_') {
                count++;
            } else {
                break;
            }
        }

        return makeMatchResult(count);
    } else {
        return nullptr;
    }
}

} // namespace Qutepart
