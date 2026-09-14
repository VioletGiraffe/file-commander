/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <QChar>
#include <QString>

namespace Qutepart {

/* The set of characters a rule can start a match with.
 *
 * Matching a context means walking every one of its rules at every column of
 * every line. Most rules can only ever match if the character under the cursor
 * belongs to a small set (a keyword's initials, a quote, a digit, ...), so
 * knowing that set up front lets the parser skip the expensive part - regexp
 * execution, hash lookups - for the vast majority of (rule, column) pairs.
 *
 * `unknown` is the safe default: a set which nothing was proven about matches
 * everything, so a rule whose first characters cannot be determined is simply
 * always tried, exactly as before.
 */
class FirstCharSet {
  public:
    inline bool mayMatch(QChar ch) const {
        if (unknown) {
            return true;
        }
        auto code = ch.unicode();
        if (code >= 128) {
            return nonAscii;
        }
        return (ascii[code >> 6] >> (code & 63)) & 1;
    }

    inline bool isUnknown() const { return unknown; }
    inline bool isNonAscii() const { return nonAscii; }
    inline quint64 asciiWord(int index) const { return ascii[index]; }

    /* Nothing is known - match everything. */
    inline void setUnknown() {
        unknown = true;
        nonAscii = true;
        ascii[0] = ascii[1] = ~quint64(0);
    }

    /* Start from "matches nothing", then add what the rule accepts. */
    inline void clear() {
        unknown = false;
        nonAscii = false;
        ascii[0] = ascii[1] = 0;
    }

    inline void addChar(QChar ch) {
        auto code = ch.unicode();
        if (code >= 128) {
            nonAscii = true;
        } else {
            ascii[code >> 6] |= quint64(1) << (code & 63);
        }
    }

    /* Adds `ch` and, when it is an ASCII letter, its other case too. */
    void addCharBothCases(QChar ch);

    void addRange(char from, char to);
    void addString(const QString &chars);
    void addDigits();
    void addAsciiLetters();
    void addWordChars();
    void addSpaces();
    inline void setNonAscii() { nonAscii = true; }

    void unite(const FirstCharSet &other);

    /* Every ASCII character which is *not* in this set (used for [^...]). */
    void complementAscii();

  private:
    quint64 ascii[2] = {~quint64(0), ~quint64(0)};
    bool nonAscii = true;
    bool unknown = true;
};

/* The set of characters `pattern` can match at its very first position.
 *
 * Returns an "unknown" set for anything the (deliberately small) parser below
 * does not fully understand, so a wrong answer degrades into "try this rule",
 * never into "skip this rule".
 */
FirstCharSet regExpFirstChars(const QString &pattern, bool caseInsensitive);

} // namespace Qutepart
