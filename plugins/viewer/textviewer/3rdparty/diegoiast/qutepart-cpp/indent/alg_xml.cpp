/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include <QDebug>
#include <QRegularExpression>

#include "indent_funcs.h"
#include "text_block_utils.h"

#include "alg_xml.h"

namespace Qutepart {

const QString &IndentAlgXml::triggerCharacters() const {
    static QString chars = "/>";
    return chars;
}

namespace {

bool matches(const QString &pattern, const QString &text) {
    return QRegularExpression(pattern).match(text).capturedLength() > 0;
}

bool isDocumentHeader(const QString &line) { return matches("^<(\\?xml|!DOCTYPE).*", line); }

bool isOpeningTag(const QString &line) { return matches("<([^/!]|[^/!][^>]*[^/])>[^<>]*$", line); }

bool isGoingToCloseTag(const QString &line) { return matches("^\\s*</", line); }

} // namespace

QString IndentAlgXml::autoFormatLine(QTextBlock block) const {
    QString lineText = block.text();

    QString prevLineText = prevNonEmptyBlock(block).text();

    QRegularExpression splitter(">\\s*<");

    auto match = splitter.match(lineText);

    if (match.capturedLength() > 0) { // if having tags to split
        QStringList newLines;
        while (match.capturedLength() > 0) {
            QString newLine = lineText.left(match.capturedStart() + 1); // +1 for >
            lineText = lineText.mid(match.capturedEnd() - 1);           // -1 for <

            // Indent new line
            QString indent = indentForLine(newLine, prevLineText);
            newLine = indent + stripLeftWhitespace(newLine);
            newLines << newLine;

            prevLineText = newLine;
            match = splitter.match(lineText);
        }

        newLines << indentForLine(lineText, prevLineText) + stripLeftWhitespace(lineText);

        return newLines.join('\n');
    }

    return indentForLine(lineText, prevLineText) + stripLeftWhitespace(lineText);
}

QString IndentAlgXml::computeSmartIndent(QTextBlock block, int /*cursorPos*/) const {
    QString lineText = block.text();
    QString prevLineText = prevNonEmptyBlock(block).text();

    return indentForLine(lineText, prevLineText);
}

QString IndentAlgXml::indentForLine(const QString &lineText, const QString &prevLineText) const {
    QString prevIndent = lineIndent(prevLineText);
    if (isDocumentHeader(prevLineText)) {
        return "";
    }

    if (lineText.isEmpty()) { // new line, use prev line to indent properly
        if (isOpeningTag(prevLineText)) {
            // increase indent when prev line opened a tag (but not for
            // comments)
            return increaseIndent(prevIndent, indentText());
        } else {
            return prevIndent;
        }
    } else if (isDocumentHeader(lineText)) {
        return "";
    } else if (isGoingToCloseTag(lineText)) {
        if (!isOpeningTag(prevLineText)) {
            // decrease indent when we write </ and prior line did not start a
            // tag
            return decreaseIndent(prevIndent, indentText());
        } else {
            return prevIndent;
        }
    }

    if (isOpeningTag(prevLineText)) {
        return increaseIndent(prevIndent, indentText());
    } else {
        return prevIndent;
    }
}

} // namespace Qutepart
