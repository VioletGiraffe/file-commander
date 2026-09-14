/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include <QHash>
#include <QMutex>

#include "text_to_match.h"

namespace Qutepart {

DeliminatorSet::DeliminatorSet(const QString &deliminators) {
    for (QChar ch : deliminators) {
        ushort code = ch.unicode();
        if (code < asciiChars_.size()) {
            asciiChars_.set(code);
        } else {
            nonAsciiChars_.append(ch);
        }
    }
}

bool DeliminatorSet::contains(QChar ch) const {
    ushort code = ch.unicode();
    if (code < asciiChars_.size()) {
        return asciiChars_.test(code);
    }
    return nonAsciiChars_.contains(ch);
}

const DeliminatorSet *sharedDeliminatorSet(const QString &deliminators) {
    auto static mutex = QMutex();
    auto static cache = QHash<QString, DeliminatorSet *>();

    QMutexLocker locker(&mutex);
    auto it = cache.constFind(deliminators);
    if (it == cache.constEnd()) {
        it = cache.insert(deliminators, new DeliminatorSet(deliminators));
    }
    return it.value();
}

TextToMatch::TextToMatch(const QString &text, const QStringList &contextData)
    : currentColumnIndex(0), wholeLineText(text), text(wholeLineText.left(wholeLineText.length())),
      textLength(text.length()), firstNonSpace(true), // copy-paste from Py code
      isWordStart(true),                              // copy-paste from Py code
      contextData(&contextData) {}

namespace {
bool isWordChar(QChar ch) { return ch.isLetterOrNumber() || ch == '_'; }
} // namespace

void TextToMatch::shiftOnce() {
    cachedWordDeliminators = nullptr;
    QChar prevChar = text.at(0);
    firstNonSpace = firstNonSpace && prevChar.isSpace();
    isWordStart = (!isWordStart) && textLength > 1 && isWordChar(text.at(1));

    currentColumnIndex++;
    text = text.right(text.length() - 1);
    textLength--;
}

void TextToMatch::shift(int count) {
    cachedWordDeliminators = nullptr;
    for (int i = 0; i < count; i++) {
        QChar prevChar = text.at(i);
        firstNonSpace = firstNonSpace && prevChar.isSpace();
        isWordStart = (!isWordStart) && textLength > (i + 1) && isWordChar(text.at(i + 1));
    }

    currentColumnIndex += count;
    text = text.right(text.length() - count);
    textLength -= count;
}

bool TextToMatch::isEmpty() const { return text.isEmpty(); }

QStringView TextToMatch::word(const DeliminatorSet *deliminators) const {
    // Several rules of a context ask for the word at the same position, one
    // after the other, and they all share the same deliminator set.
    if (cachedWordDeliminators == deliminators) {
        return cachedWord;
    }
    cachedWordDeliminators = deliminators;
    cachedWord = QStringView();

    if (currentColumnIndex > 0) {
        QChar prevChar = wholeLineText[currentColumnIndex - 1];
        if (!deliminators->contains(prevChar)) {
            return cachedWord;
        }
    }

    int wordEndIndex = 0;
    for (; wordEndIndex < text.length(); wordEndIndex++) {
        if (deliminators->contains(text.at(wordEndIndex))) {
            break;
        }
    }
    if (wordEndIndex != 0) {
        cachedWord = text.left(wordEndIndex);
    }

    return cachedWord;
}

} // namespace Qutepart
