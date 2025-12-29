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
            break;
        }
    }
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
    for (auto i = start; i < start + length; i++) {
        textTypeMap[i] = textType;
    }
}

// Helper function for parseBlock()
void Context::applyMatchResult(const TextToMatch &textToMatch, const MatchResult *matchRes,
                               const Context *context, QVector<QTextLayout::FormatRange> &formats,
                               QString &textTypeMap) const {
    auto displayFormat = matchRes->style.format();

    if (displayFormat.isNull()) {
        displayFormat = context->style.format();
    }

    if (!displayFormat.isNull()) {
        appendFormat(formats, textToMatch.currentColumnIndex, matchRes->length, *displayFormat);
    }

    QChar textType = matchRes->style.textType();
    if (textType == 0) {
        textType = context->style.textType();
    }
    fillTextTypeMap(textTypeMap, textToMatch.currentColumnIndex, matchRes->length, textType);
}

// Parse block. Exits, when reached end of the text, or when context is switched
const ContextStack Context::parseBlock(const ContextStack &contextStack, TextToMatch &textToMatch,
                                       QVector<QTextLayout::FormatRange> &formats,
                                       QString &textTypeMap, bool &lineContinue,
                                       TextBlockUserData *data) const {
    textToMatch.contextData = &contextStack.currentData();

    if (textToMatch.isEmpty() && (!_lineEmptyContext.isNull())) {
        return contextStack.switchContext(_lineEmptyContext);
    }

    while (!textToMatch.isEmpty()) {
        auto matchRes = tryMatch(textToMatch);

        if (matchRes) {
            lineContinue = matchRes->lineContinue;

            if (data && !matchRes->rule->beginRegion.isEmpty()) {
                data->regions.push(matchRes->rule->beginRegion);
            }

            if (data && !matchRes->rule->endRegion.isEmpty()) {
                if (!data->regions.isEmpty() && data->regions.top() == matchRes->rule->endRegion) {
                    data->regions.pop();
                }
            }

            if (data) {
                data->folding.level = data->regions.size();
            }

            if (matchRes->nextContext.isNull()) {
                applyMatchResult(textToMatch, matchRes, this, formats, textTypeMap);
                textToMatch.shift(matchRes->length);
                delete matchRes;
            } else {
                ContextStack newContextStack =
                    contextStack.switchContext(matchRes->nextContext, matchRes->data);

                applyMatchResult(textToMatch, matchRes, newContextStack.currentContext(), formats,
                                 textTypeMap);
                textToMatch.shift(matchRes->length);
                delete matchRes;
                return newContextStack;
            }
        } else {
            lineContinue = false;
            if (!style.format().isNull()) {
                appendFormat(formats, textToMatch.currentColumnIndex, 1, *style.format());
            }
            textTypeMap[textToMatch.currentColumnIndex] = style.textType();
            if (!this->fallthroughContext.isNull()) {
                return contextStack.switchContext(this->fallthroughContext);
            }
            textToMatch.shiftOnce();
        }
    }

    return contextStack;
}

MatchResult *Context::tryMatch(const TextToMatch &textToMatch) const {
    for (auto &rule : rules) {
        MatchResult *matchRes = rule->tryMatch(textToMatch);
        if (matchRes != nullptr) {
            return matchRes;
        }
    }

    return nullptr;
}

} // namespace Qutepart
