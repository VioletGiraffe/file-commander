/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "first_chars.h"

namespace Qutepart {

void FirstCharSet::addCharBothCases(QChar ch) {
    addChar(ch);
    auto code = ch.unicode();
    if (code >= 'a' && code <= 'z') {
        addChar(QChar(code - 'a' + 'A'));
    } else if (code >= 'A' && code <= 'Z') {
        addChar(QChar(code - 'A' + 'a'));
    }
}

void FirstCharSet::addRange(char from, char to) {
    for (auto code = int(from); code <= int(to); code++) {
        addChar(QChar(code));
    }
}

void FirstCharSet::addString(const QString &chars) {
    for (auto ch : chars) {
        addChar(ch);
    }
}

void FirstCharSet::addDigits() { addRange('0', '9'); }

void FirstCharSet::addAsciiLetters() {
    addRange('a', 'z');
    addRange('A', 'Z');
}

void FirstCharSet::addWordChars() {
    addAsciiLetters();
    addDigits();
    addChar(QChar('_'));
}

void FirstCharSet::addSpaces() {
    addChar(QChar(' '));
    addRange('\t', '\r');
}

void FirstCharSet::unite(const FirstCharSet &other) {
    if (other.unknown) {
        setUnknown();
        return;
    }
    if (unknown) {
        return;
    }
    ascii[0] |= other.ascii[0];
    ascii[1] |= other.ascii[1];
    nonAscii = nonAscii || other.nonAscii;
}

void FirstCharSet::complementAscii() {
    if (unknown) {
        return;
    }
    ascii[0] = ~ascii[0];
    ascii[1] = ~ascii[1];
    // A negated class lists ASCII characters far more often than not; assuming
    // it also accepts everything above ASCII only ever costs an extra attempt.
    nonAscii = true;
}

namespace {

/* Recursive-descent FIRST-set computation for the (small) subset of PCRE the
 * syntax definitions actually use. Every construct which is not fully
 * understood raises `failed`, which turns the whole answer into "unknown".
 */
class RegExpScanner {
  public:
    RegExpScanner(const QString &pattern, bool caseInsensitive)
        : pattern(pattern), caseInsensitive(caseInsensitive) {}

    auto run() -> FirstCharSet {
        FirstCharSet result;
        auto nullable = false;
        parseAlternation(result, nullable, 0);

        if (failed || pos != pattern.length()) {
            result.setUnknown();
        }
        return result;
    }

  private:
    static constexpr int MaxDepth = 12;

    const QString &pattern;
    bool caseInsensitive;
    int pos = 0;
    bool failed = false;

    inline bool atEnd() const { return pos >= pattern.length(); }
    inline QChar at(int index) const { return pattern.at(index); }
    inline QChar current() const { return pattern.at(pos); }

    void addLiteral(FirstCharSet &out, QChar ch) const {
        if (caseInsensitive) {
            out.addCharBothCases(ch);
            // Unicode case folding can map an ASCII letter onto a character
            // outside ASCII (LATIN SMALL LETTER LONG S, KELVIN SIGN, ...).
            out.setNonAscii();
        } else {
            out.addChar(ch);
        }
    }

    void parseAlternation(FirstCharSet &out, bool &nullable, int depth) {
        out.clear();
        nullable = false;

        for (;;) {
            FirstCharSet alternative;
            auto alternativeNullable = false;
            parseSequence(alternative, alternativeNullable, depth);
            if (failed) {
                return;
            }
            out.unite(alternative);
            nullable = nullable || alternativeNullable;

            if (!atEnd() && current() == '|') {
                pos++;
                continue;
            }
            return;
        }
    }

    void parseSequence(FirstCharSet &out, bool &nullable, int depth) {
        out.clear();
        nullable = true;

        while (!atEnd() && current() != '|' && current() != ')') {
            FirstCharSet atom;
            auto atomNullable = false;
            auto zeroWidth = false;
            parseAtom(atom, atomNullable, zeroWidth, depth);
            if (failed) {
                return;
            }
            if (zeroWidth) {
                continue;
            }
            out.unite(atom);
            if (!atomNullable) {
                /* This atom always consumes a character, so nothing after it can
                 * widen the set. Skip the rest rather than parse it - a tail we
                 * do not understand must not cost us the answer we already have.
                 */
                nullable = false;
                skipRemainderOfAlternative();
                return;
            }
        }
    }

    /* Advances to the '|' or ')' which ends the current alternative. */
    void skipRemainderOfAlternative() {
        auto depth = 0;
        while (!atEnd()) {
            auto ch = current();
            if (ch == '\\') {
                pos += 2;
                continue;
            }
            if (ch == '[') {
                skipCharClass();
                if (failed) {
                    return;
                }
                continue;
            }
            if (ch == '(') {
                depth++;
            } else if (ch == ')') {
                if (depth == 0) {
                    return;
                }
                depth--;
            } else if (ch == '|' && depth == 0) {
                return;
            }
            pos++;
        }
    }

    void parseAtom(FirstCharSet &out, bool &atomNullable, bool &zeroWidth, int depth) {
        out.clear();
        atomNullable = false;
        zeroWidth = false;

        auto ch = current();
        switch (ch.unicode()) {
        case '^':
        case '$':
            pos++;
            zeroWidth = true;
            return;

        case '(':
            parseGroup(out, atomNullable, zeroWidth, depth);
            return;

        case '[':
            parseCharClass(out);
            if (failed) {
                return;
            }
            atomNullable = parseQuantifier();
            return;

        case '\\':
            pos++;
            if (atEnd()) {
                failed = true;
                return;
            }
            parseEscape(out, zeroWidth, false);
            if (failed) {
                return;
            }
            atomNullable = parseQuantifier();
            return;

        case '.':
            pos++;
            out.setUnknown();
            atomNullable = parseQuantifier();
            return;

        case '*':
        case '+':
        case '?':
            failed = true; // quantifier without an atom
            return;

        default:
            pos++;
            addLiteral(out, ch);
            atomNullable = parseQuantifier();
            return;
        }
    }

    void parseGroup(FirstCharSet &out, bool &atomNullable, bool &zeroWidth, int depth) {
        if (depth >= MaxDepth) {
            failed = true;
            return;
        }

        pos++; // '('
        if (!atEnd() && current() == '?') {
            pos++;
            if (atEnd()) {
                failed = true;
                return;
            }
            auto kind = current();
            if (kind == ':') {
                pos++;
            } else if (kind == '=' || kind == '!' || kind == '>') {
                // Lookahead / atomic group. Lookaheads consume nothing; an atomic
                // group does, but skipping it conservatively costs only speed.
                skipGroupBody();
                if (failed) {
                    return;
                }
                zeroWidth = true;
                parseQuantifier();
                return;
            } else if (kind == '<' && pos + 1 < pattern.length() &&
                       (at(pos + 1) == '=' || at(pos + 1) == '!')) {
                skipGroupBody();
                if (failed) {
                    return;
                }
                zeroWidth = true;
                parseQuantifier();
                return;
            } else if (kind == '<' || kind == '\'') {
                auto closing = kind == '<' ? QChar('>') : QChar('\'');
                pos++;
                while (!atEnd() && current() != closing) {
                    pos++;
                }
                if (atEnd()) {
                    failed = true;
                    return;
                }
                pos++;
            } else if (kind == '#') {
                skipGroupBody();
                zeroWidth = true;
                return;
            } else {
                failed = true; // inline options such as (?i), conditionals, ...
                return;
            }
        }

        auto innerNullable = false;
        parseAlternation(out, innerNullable, depth + 1);
        if (failed) {
            return;
        }
        if (atEnd() || current() != ')') {
            failed = true;
            return;
        }
        pos++;

        atomNullable = innerNullable || parseQuantifier();
    }

    /* Skips from inside a group to just past its closing parenthesis. */
    void skipGroupBody() {
        auto depth = 1;
        while (!atEnd()) {
            auto ch = current();
            if (ch == '\\') {
                pos += 2;
                continue;
            }
            if (ch == '[') {
                skipCharClass();
                if (failed) {
                    return;
                }
                continue;
            }
            pos++;
            if (ch == '(') {
                depth++;
            } else if (ch == ')') {
                depth--;
                if (depth == 0) {
                    return;
                }
            }
        }
        failed = true;
    }

    void skipCharClass() {
        pos++; // '['
        if (!atEnd() && current() == '^') {
            pos++;
        }
        while (!atEnd()) {
            if (current() == '\\') {
                pos += 2;
                continue;
            }
            if (current() == ']') {
                pos++;
                return;
            }
            pos++;
        }
        failed = true;
    }

    void parseCharClass(FirstCharSet &out) {
        pos++; // '['
        auto negated = false;
        if (!atEnd() && current() == '^') {
            negated = true;
            pos++;
        }

        FirstCharSet set;
        set.clear();
        auto empty = true;

        while (!atEnd() && current() != ']') {
            if (current() == '[' && pos + 1 < pattern.length() && at(pos + 1) == ':') {
                failed = true; // POSIX class
                return;
            }

            FirstCharSet item;
            item.clear();
            auto isLiteral = false;
            QChar literal;

            if (current() == '\\') {
                pos++;
                if (atEnd()) {
                    failed = true;
                    return;
                }
                auto zeroWidth = false;
                parseEscape(item, zeroWidth, true);
                if (failed) {
                    return;
                }
                // An escape as a range bound ("[\\x41-\\x5a]") is rare enough to
                // not be worth modelling - and guessing would under-approximate.
                if (!atEnd() && current() == '-' && pos + 1 < pattern.length() &&
                    at(pos + 1) != ']') {
                    failed = true;
                    return;
                }
            } else {
                literal = current();
                isLiteral = true;
                pos++;
                item.addChar(literal);
            }

            empty = false;

            if (isLiteral && !atEnd() && current() == '-' && pos + 1 < pattern.length() &&
                at(pos + 1) != ']') {
                pos++;
                if (current() == '\\') {
                    failed = true; // escaped range bound - not worth supporting
                    return;
                }
                auto high = current();
                pos++;
                if (high.unicode() < literal.unicode()) {
                    failed = true;
                    return;
                }
                for (auto code = literal.unicode(); code <= high.unicode(); code++) {
                    if (caseInsensitive) {
                        set.addCharBothCases(QChar(code));
                    } else {
                        set.addChar(QChar(code));
                    }
                }
                if (high.unicode() >= 128) {
                    set.setNonAscii();
                }
                continue;
            }

            if (isLiteral) {
                if (caseInsensitive) {
                    addLiteral(set, literal);
                } else {
                    set.addChar(literal);
                }
            } else {
                set.unite(item);
            }
        }

        if (atEnd() || empty) {
            failed = true;
            return;
        }
        pos++; // ']'

        if (negated) {
            set.complementAscii();
        } else if (caseInsensitive) {
            set.setNonAscii();
        }
        out = set;
    }

    static QChar escapedLiteral(QChar ch) {
        switch (ch.unicode()) {
        case 'n':
            return QChar('\n');
        case 't':
            return QChar('\t');
        case 'r':
            return QChar('\r');
        case 'f':
            return QChar('\f');
        case 'v':
            return QChar('\v');
        case 'a':
            return QChar(0x07);
        case 'e':
            return QChar(0x1b);
        case '0':
            return QChar(QChar::Null);
        default:
            return ch;
        }
    }

    /* `pos` points at the character following the backslash. */
    void parseEscape(FirstCharSet &out, bool &zeroWidth, bool insideCharClass) {
        auto ch = current();
        pos++;

        switch (ch.unicode()) {
        case 'd':
            out.addDigits();
            out.setNonAscii();
            return;
        case 'w':
            out.addWordChars();
            out.setNonAscii();
            return;
        case 's':
            out.addSpaces();
            out.setNonAscii();
            return;
        case 'D':
        case 'W':
        case 'S':
        case 'h':
        case 'H':
        case 'v':
        case 'V':
        case 'R':
        case 'N':
        case 'p':
        case 'P':
        case 'X':
            out.setUnknown();
            return;

        case 'b':
            if (insideCharClass) {
                out.addChar(QChar(0x08));
                return;
            }
            zeroWidth = true;
            return;
        case 'B':
        case 'A':
        case 'Z':
        case 'z':
        case 'G':
        case 'K':
            if (insideCharClass) {
                failed = true;
                return;
            }
            zeroWidth = true;
            return;

        case 'x':
            parseHexEscape(out);
            return;

        case 'Q':
        case 'E':
        case 'c':
        case 'g':
        case 'k':
            failed = true;
            return;

        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            (void)insideCharClass;
            failed = true; // back reference, or an octal escape
            return;

        default:
            if (ch.isLetterOrNumber() && ch.unicode() < 128 && escapedLiteral(ch) == ch) {
                failed = true; // unknown alphanumeric escape
                return;
            }
            if (caseInsensitive) {
                out.addCharBothCases(escapedLiteral(ch));
                out.setNonAscii();
            } else {
                out.addChar(escapedLiteral(ch));
            }
            return;
        }
    }

    void parseHexEscape(FirstCharSet &out) {
        auto value = 0;
        if (!atEnd() && current() == '{') {
            pos++;
            auto digits = 0;
            while (!atEnd() && current() != '}') {
                auto digit = hexValue(current());
                if (digit < 0) {
                    failed = true;
                    return;
                }
                value = value * 16 + digit;
                digits++;
                pos++;
                if (value > 0x10ffff) {
                    failed = true;
                    return;
                }
            }
            if (atEnd() || digits == 0) {
                failed = true;
                return;
            }
            pos++; // '}'
        } else {
            auto digits = 0;
            while (digits < 2 && !atEnd() && hexValue(current()) >= 0) {
                value = value * 16 + hexValue(current());
                pos++;
                digits++;
            }
            if (digits == 0) {
                value = 0;
            }
        }

        if (value > 0xffff) {
            out.setUnknown(); // surrogate pair; not worth modelling
            return;
        }
        if (caseInsensitive) {
            out.addCharBothCases(QChar(value));
            out.setNonAscii();
        } else {
            out.addChar(QChar(value));
        }
    }

    static int hexValue(QChar ch) {
        auto code = ch.unicode();
        if (code >= '0' && code <= '9') {
            return code - '0';
        }
        if (code >= 'a' && code <= 'f') {
            return code - 'a' + 10;
        }
        if (code >= 'A' && code <= 'F') {
            return code - 'A' + 10;
        }
        return -1;
    }

    /* Consumes a quantifier, if any. Returns true when it makes the preceding
     * atom optional. */
    bool parseQuantifier() {
        if (atEnd()) {
            return false;
        }

        auto optional = false;
        switch (current().unicode()) {
        case '?':
        case '*':
            optional = true;
            pos++;
            break;
        case '+':
            pos++;
            break;
        case '{': {
            auto save = pos;
            pos++;
            auto digits = 0;
            auto low = 0;
            while (!atEnd() && current().isDigit()) {
                low = low * 10 + (current().unicode() - '0');
                digits++;
                pos++;
            }
            if (digits == 0) {
                pos = save; // a literal '{'
                return false;
            }
            if (!atEnd() && current() == ',') {
                pos++;
                while (!atEnd() && current().isDigit()) {
                    pos++;
                }
            }
            if (atEnd() || current() != '}') {
                pos = save; // a literal '{'
                return false;
            }
            pos++;
            optional = low == 0;
            break;
        }
        default:
            return false;
        }

        // Lazy / possessive suffix.
        if (!atEnd() && (current() == '?' || current() == '+')) {
            pos++;
        }
        return optional;
    }
};

} // namespace

FirstCharSet regExpFirstChars(const QString &pattern, bool caseInsensitive) {
    FirstCharSet result;
    if (pattern.isEmpty()) {
        return result; // stays unknown
    }
    RegExpScanner scanner(pattern, caseInsensitive);
    return scanner.run();
}

} // namespace Qutepart
