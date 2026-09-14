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

void AbstractRule::ensureFirstChars(int depth) {
    if (firstCharsReady) {
        return;
    }
    computeFirstChars(depth);
    firstCharsReady = true;
}

void AbstractRule::finalize() {
    // Capture groups are only ever read back through the stack entry pushed by
    // a context switch, so a rule which does not push one cannot need them.
    auto target = contextSwitcher.context();
    capturesNeeded = !target.isNull() && target->usesCaptures();
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

bool AbstractRule::makeMatchResult(MatchResult &result, int length, bool lineContinue) const {
    result.length = lookAhead ? 0 : length;
    result.lineContinue = lineContinue;
    result.nextContext = &contextSwitcher;
    result.style = &style;
    result.rule = this;
    if (!result.data.isEmpty()) {
        result.data.clear(); // do not leak captures from a previously matched rule
    }
    return true;
}

bool AbstractRule::tryMatch(const TextToMatch &textToMatch, MatchResult &result) const {
    if (column != -1 && column != textToMatch.currentColumnIndex) {
        return false;
    }

    if (firstNonSpace && (not textToMatch.firstNonSpace)) {
        return false;
    }

    return tryMatchImpl(textToMatch, result);
}

AbstractStringRule::AbstractStringRule(const AbstractRuleParams &params, const QString &value,
                                       bool insensitive)
    : AbstractRule(params), value(value), insensitive(insensitive) {
    dropDynamicWithoutPlaceholders(value);
}

QString AbstractStringRule::args() const {
    QString result = value;
    if (insensitive) {
        result += " insensitive";
    }

    return result;
}

void AbstractRule::dropDynamicWithoutPlaceholders(const QString &pattern) {
    if (!dynamic) {
        return;
    }
    for (auto index = 0; index + 1 < pattern.length(); index++) {
        if (pattern.at(index) == QLatin1Char('%') && pattern.at(index + 1).isDigit()) {
            return;
        }
    }
    dynamic = false;
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

bool StringDetectRule::tryMatchImpl(const TextToMatch &textToMatch, MatchResult &result) const {
    QString pattern = value;
    if (dynamic) {
        pattern = makeDynamicSubsctitutions(value, *textToMatch.contextData);
    }

    if (pattern.isEmpty()) {
        return false;
    }

    if (textToMatch.text.startsWith(pattern)) {
        return makeMatchResult(result, pattern.length());
    }

    return false;
}

KeywordRule::KeywordRule(const AbstractRuleParams &params, const QString &listName)
    : AbstractRule(params), listName(listName), caseSensitive(true) {}

void KeywordRule::setKeywordParams(const QHash<QString, QStringList> &lists, bool newCaseSensitive,
                                   const QString &newDeliminators, QString &error) {
    if (!lists.contains(listName)) {
        error = QString("List '%1' not found").arg(error);
        return;
    }
    this->caseSensitive = newCaseSensitive;
    this->deliminators = sharedDeliminatorSet(newDeliminators);

    items.clear();
    const auto &list = lists[listName];
    items.reserve(list.size());
    for (const auto &word : list) {
        items.insert(this->caseSensitive ? word : word.toLower(), true);
    }
}

bool KeywordRule::tryMatchImpl(const TextToMatch &textToMatch, MatchResult &result) const {
    QStringView word = textToMatch.word(deliminators);

    if (word.isEmpty()) {
        return false;
    }

    bool matched = false;
    if (this->caseSensitive) {
        matched = items.contains(word);
    } else {
        matched = items.contains(word.toString().toLower());
    }

    if (matched) {
        return makeMatchResult(result, word.length());
    } else {
        return false;
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

bool DetectCharRule::tryMatchImpl(const TextToMatch &textToMatch, MatchResult &result) const {
    QChar pattern = value;

    if (dynamic) {
        int matchIndex = this->index - 1;
        if (textToMatch.contextData == nullptr) {
            qWarning() << "Dynamic DetectCharRule but no data";
        }

        if (matchIndex >= textToMatch.contextData->length()) {
            qWarning() << "Invalid DetectChar index" << matchIndex;
            return false;
        }

        if (textToMatch.contextData->at(matchIndex).length() != 1) {
            qWarning() << "Too long DetectChar string " << *textToMatch.contextData;
            return false;
        }

        pattern = textToMatch.contextData->at(matchIndex)[0];
    }

    if (textToMatch.text.at(0) == pattern) {
        return makeMatchResult(result, 1);
    } else {
        return false;
    }
}

bool Detect2CharsRule::tryMatchImpl(const TextToMatch &textToMatch, MatchResult &result) const {
    if (textToMatch.text.startsWith(value)) {
        return makeMatchResult(result, 2);
    }

    return false;
}

bool AnyCharRule::tryMatchImpl(const TextToMatch &textToMatch, MatchResult &result) const {
    if (value.contains(textToMatch.text.at(0))) {
        return makeMatchResult(result, 1);
    }

    return false;
}

bool WordDetectRule::tryMatchImpl(const TextToMatch &textToMatch, MatchResult &result) const {
    QStringView word = textToMatch.word(mDeliminatorSet);
    if (word.isEmpty()) {
        return false;
    }

    bool matched = insensitive ? word.compare(value, Qt::CaseInsensitive) == 0 : word == value;

    if (matched) {
        return makeMatchResult(result, word.length());
    } else {
        return false;
    }
}

void WordDetectRule::setKeywordParams(const QHash<QString, QStringList> &, bool,
                                      const QString &deliminatorSet, QString &) {
    mDeliminatorSet = sharedDeliminatorSet(deliminatorSet);
}

RegExpRule::RegExpRule(const AbstractRuleParams &params, const QString &value, bool insensitive,
                       bool minimal, bool wordStart, bool lineStart)
    : AbstractRule(params), value(value), insensitive(insensitive), minimal(minimal),
      wordStart(wordStart), lineStart(lineStart) {
    dropDynamicWithoutPlaceholders(value);
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

bool RegExpRule::tryMatchImpl(const TextToMatch &textToMatch, MatchResult &result) const {
    // Special case. if pattern starts with \b, we have to check it manually,
    // because string is passed to .match(..) without beginning
    if (wordStart && (!textToMatch.isWordStart)) {
        return false;
    }

    // Special case. If pattern starts with ^ - check column number manually
    if (lineStart && textToMatch.currentColumnIndex > 0) {
        return false;
    }

    QRegularExpressionMatch match;
    if (dynamic) {
        auto pattern = makeDynamicSubsctitutions(value, *textToMatch.contextData);
        if (pattern != dynamicPattern) {
            dynamicPattern = pattern;
            dynamicRegExp = compileRegExp(pattern);
        }
        match = dynamicRegExp.matchView(textToMatch.text, 0, QRegularExpression::NormalMatch,
                                        QRegularExpression::AnchorAtOffsetMatchOption);
    } else {
        match = regExp.matchView(textToMatch.text, 0, QRegularExpression::NormalMatch,
                                 QRegularExpression::AnchorAtOffsetMatchOption);
    }

    if (match.hasMatch() && match.capturedLength() > 0) {
        makeMatchResult(result, match.capturedLength(), false);
        if (capturesNeeded) {
            result.data = match.capturedTexts();
        }
        return true;
    } else {
        return false;
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

bool AbstractNumberRule::tryMatchImpl(const TextToMatch &textToMatch, MatchResult &result) const {
    // andreikop: This condition is not described in kate docs, and I haven't
    // found it in the code
    if (!textToMatch.isWordStart) {
        return false;
    }

    int matchedLength = tryMatchText(textToMatch.text);

    if (matchedLength <= 0) {
        return false;
    }

    if (matchedLength < textToMatch.text.length()) {
        TextToMatch textToMatchCopy = textToMatch;
        textToMatchCopy.shift(matchedLength);

        MatchResult childResult;
        for (auto const &rule : std::as_const(childRules)) {
            if (rule->tryMatch(textToMatchCopy, childResult)) {
                matchedLength += childResult.length;
                break;
            }
            // child rule context and attribute ignored
        }
    }

    return makeMatchResult(result, matchedLength);
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

bool HlCOctRule::tryMatchImpl(const TextToMatch &textToMatch, MatchResult &result) const {
    if (textToMatch.text.at(0) != '0') {
        return false;
    }

    int index = 1;
    while (index < textToMatch.text.length() && isOctal(textToMatch.text.at(index))) {
        index++;
    }

    if (index == 1) {
        return false;
    }

    if (index < textToMatch.text.length() && isNumberLengthSpecifier(textToMatch.text.at(index))) {
        index++;
    }

    return makeMatchResult(result, index);
}

bool HlCHexRule::tryMatchImpl(const TextToMatch &textToMatch, MatchResult &result) const {
    if (textToMatch.text.length() < 3) {
        return false;
    }

    if (textToMatch.text.at(0) != '0' || textToMatch.text.at(1).toUpper() != 'X') {
        return false;
    }

    int index = 2;
    while (index < textToMatch.text.length() && isHex(textToMatch.text.at(index))) {
        index++;
    }

    if (index == 2) {
        return false;
    }

    if (index < textToMatch.text.length() && isNumberLengthSpecifier(textToMatch.text.at(index))) {
        index++;
    }

    return makeMatchResult(result, index);
}

bool HlCStringCharRule::tryMatchImpl(const TextToMatch &textToMatch, MatchResult &result) const {
    int res = checkEscapedChar(textToMatch.text);
    if (res != -1) {
        return makeMatchResult(result, res);
    } else {
        return false;
    }
}

bool HlCCharRule::tryMatchImpl(const TextToMatch &textToMatch, MatchResult &result) const {
    if (textToMatch.text.length() > 2 && textToMatch.text.at(0) == '\'' &&
        textToMatch.text.at(1) != '\'') {
        int index = 0;
        int escapedLen = checkEscapedChar(textToMatch.text.mid(1));
        if (escapedLen != -1) {
            index = 1 + escapedLen;
        } else { // 1 not escaped character
            index = 1 + 1;
        }

        if (index < textToMatch.text.length() && textToMatch.text.at(index) == '\'') {
            return makeMatchResult(result, index + 1);
        }
    }

    return false;
}

RangeDetectRule::RangeDetectRule(const AbstractRuleParams &params, const QString &char0,
                                 const QString &char1)
    : AbstractRule(params), char0(char0), char1(char1) {}

QString RangeDetectRule::args() const { return QString("%1 - %2").arg(char0, char1); }

bool RangeDetectRule::tryMatchImpl(const TextToMatch &textToMatch, MatchResult &result) const {
    if (textToMatch.text.startsWith(char0)) {
        int end = textToMatch.text.indexOf(char1, 1);
        if (end > 0) {
            return makeMatchResult(result, end + 1);
        }
    }

    return false;
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

void IncludeRulesRule::computeFirstChars(int depth) {
    // An include matches exactly what the included context's own rules match.
    if (context.isNull()) {
        firstChars.setUnknown();
        return;
    }
    firstChars = context->firstCharsOfRules(depth);
}

bool IncludeRulesRule::readsCaptures() const {
    // The included rules run with the *including* context on top of the stack,
    // so they read that context's captures. An unresolved include is assumed to
    // need them.
    return dynamic || context.isNull() || context->usesCaptures();
}

bool IncludeRulesRule::tryMatchImpl(const TextToMatch &textToMatch, MatchResult &result) const {
    if (context == nullptr) {
        qWarning() << "IncludeRules called for null context" << description();
        return false;
    }

    return context->tryMatch(textToMatch, result);
}

bool LineContinueRule::tryMatchImpl(const TextToMatch &textToMatch, MatchResult &result) const {
    if (textToMatch.text == QLatin1String("\\")) {
        return makeMatchResult(result, 1, true);
    }

    return false;
}

bool DetectSpacesRule::tryMatchImpl(const TextToMatch &textToMatch, MatchResult &result) const {
    int index = 0;
    while (index < textToMatch.text.length() && textToMatch.text.at(index).isSpace()) {
        index++;
    }

    if (index > 0) {
        return makeMatchResult(result, index);
    } else {
        return false;
    }
}

bool DetectIdentifierRule::tryMatchImpl(const TextToMatch &textToMatch, MatchResult &result) const {
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

        return makeMatchResult(result, count);
    } else {
        return false;
    }
}

/* ---------------------------------------------------------------------------
 * First-character sets
 *
 * Every rule declares which characters it could possibly match at, so that
 * Context::tryMatch() can skip it outright at every other column. Anything not
 * described here keeps the base implementation, which claims every character
 * and therefore behaves exactly as before.
 * ------------------------------------------------------------------------- */

void KeywordRule::computeFirstChars(int depth) {
    (void)depth;
    firstChars.clear();
    for (auto it = items.constBegin(); it != items.constEnd(); ++it) {
        const auto &word = it.key();
        if (word.isEmpty()) {
            continue;
        }
        if (caseSensitive) {
            firstChars.addChar(word.at(0));
        } else {
            // Keywords are stored lower-cased and the candidate word is
            // lower-cased before the lookup, so either case can start a match.
            firstChars.addCharBothCases(word.at(0));
        }
    }
}

void DetectCharRule::computeFirstChars(int depth) {
    (void)depth;
    if (dynamic) {
        firstChars.setUnknown();
        return;
    }
    firstChars.clear();
    firstChars.addChar(value);
}

void Detect2CharsRule::computeFirstChars(int depth) {
    (void)depth;
    if (value.isEmpty()) {
        firstChars.setUnknown();
        return;
    }
    firstChars.clear();
    firstChars.addChar(value.at(0));
}

void AnyCharRule::computeFirstChars(int depth) {
    (void)depth;
    firstChars.clear();
    firstChars.addString(value);
}

void StringDetectRule::computeFirstChars(int depth) {
    (void)depth;
    if (dynamic || value.isEmpty()) {
        firstChars.setUnknown();
        return;
    }
    firstChars.clear();
    firstChars.addChar(value.at(0));
}

void WordDetectRule::computeFirstChars(int depth) {
    (void)depth;
    if (value.isEmpty()) {
        firstChars.setUnknown();
        return;
    }
    firstChars.clear();
    if (insensitive) {
        firstChars.addCharBothCases(value.at(0));
        firstChars.setNonAscii();
    } else {
        firstChars.addChar(value.at(0));
    }
}

void RegExpRule::computeFirstChars(int depth) {
    (void)depth;
    if (dynamic) {
        firstChars.setUnknown();
        return;
    }
    firstChars = regExpFirstChars(value, insensitive);
}

void IntRule::computeFirstChars(int depth) {
    (void)depth;
    firstChars.clear();
    firstChars.addDigits();
}

void FloatRule::computeFirstChars(int depth) {
    (void)depth;
    firstChars.clear();
    firstChars.addDigits();
    // tryMatchText() also accepts a leading '.' and a bare exponent.
    firstChars.addChar(QChar('.'));
    firstChars.addChar(QChar('e'));
    firstChars.addChar(QChar('E'));
}

void HlCOctRule::computeFirstChars(int depth) {
    (void)depth;
    firstChars.clear();
    firstChars.addChar(QChar('0'));
}

void HlCHexRule::computeFirstChars(int depth) {
    (void)depth;
    firstChars.clear();
    firstChars.addChar(QChar('0'));
}

void HlCStringCharRule::computeFirstChars(int depth) {
    (void)depth;
    firstChars.clear();
    firstChars.addChar(QChar('\\'));
}

void HlCCharRule::computeFirstChars(int depth) {
    (void)depth;
    firstChars.clear();
    firstChars.addChar(QChar('\''));
}

void RangeDetectRule::computeFirstChars(int depth) {
    (void)depth;
    if (char0.isEmpty()) {
        firstChars.setUnknown();
        return;
    }
    firstChars.clear();
    firstChars.addChar(char0.at(0));
}

void LineContinueRule::computeFirstChars(int depth) {
    (void)depth;
    firstChars.clear();
    firstChars.addChar(QChar('\\'));
}

void DetectSpacesRule::computeFirstChars(int depth) {
    (void)depth;
    firstChars.clear();
    firstChars.addSpaces();
    firstChars.setNonAscii(); // QChar::isSpace() is not limited to ASCII
}

void DetectIdentifierRule::computeFirstChars(int depth) {
    (void)depth;
    firstChars.clear();
    firstChars.addAsciiLetters();
    firstChars.setNonAscii(); // QChar::isLetter() is not limited to ASCII
}

} // namespace Qutepart
