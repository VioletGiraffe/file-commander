/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <bitset>

#include <QString>
#include <QStringView>

namespace Qutepart {

/* A set of "word deliminator" characters, with O(1) membership testing.
 * Built once (when a rule's deliminator string is set) and reused on every
 * character checked while extracting a word - avoids rescanning the
 * deliminator string for every character of every candidate word.
 */
class DeliminatorSet {
  public:
    DeliminatorSet() = default;
    explicit DeliminatorSet(const QString &deliminators);

    bool contains(QChar ch) const;

  private:
    std::bitset<128> asciiChars_;
    QString nonAsciiChars_; // deliminators outside ASCII range; expected to be empty in practice
};

/* Returns a shared,long-lived set for `deliminators`.
 *
 * Every keyword-ish rule of a language is built from the same deliminator
 * string, so handing them all the same object lets TextToMatch::word() cache
 * its answer and hand it to each of them in turn instead of re-scanning the
 * word once per rule.
 */
const DeliminatorSet *sharedDeliminatorSet(const QString &deliminators);

/* Peace of text, which shall be matched.
 * Contains pre-calculated and pre-checked data for performance optimization
 */
class TextToMatch {
  public:
    TextToMatch(const QString &text, const QStringList &contextData);

    void shiftOnce();
    void shift(int count);

    bool isEmpty() const;

    QStringView word(const DeliminatorSet *deliminators) const;

    int currentColumnIndex;
    QString wholeLineText;
    QStringView text;
    int textLength;
    bool firstNonSpace;
    bool isWordStart;
    const QStringList *contextData;

  private:
    // The word at the current position, remembered for the rules that follow.
    mutable const DeliminatorSet *cachedWordDeliminators = nullptr;
    mutable QStringView cachedWord;
};

} // namespace Qutepart
