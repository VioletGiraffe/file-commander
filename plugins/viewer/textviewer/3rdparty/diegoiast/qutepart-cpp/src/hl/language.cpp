/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include <algorithm>

#include "context_switcher.h"
#include "language.h"
#include "text_block_user_data.h"
#include "text_to_match.h"

namespace Qutepart {

Language::Language(const QString &name, const QStringList &extensions, const QStringList &mimetypes,
                   int priority, bool hidden, const QString &indenter,
                   QString startMultilineComment, QString endMultilineComment,
                   QString singleLineComment, const QSet<QString> &allLanguageKeywords,
                   const QList<ContextPtr> &contexts)
    : name(name), startMultilineComment(startMultilineComment),
      endMultilineComment(endMultilineComment), singleLineComment(singleLineComment),
      extensions(extensions), mimetypes(mimetypes), priority(priority), hidden(hidden),
      indenter(indenter), allLanguageKeywords_(allLanguageKeywords), contexts(contexts),
      defaultContextStack(contexts[0].data()) {}

void Language::printDescription(QTextStream &out) const {
    out << "Language " << name << "\n";
    out << "\textensions: " << extensions.join(", ") << "\n";
    if (!mimetypes.isEmpty()) {
        out << "\tmimetypes: " << mimetypes.join(", ") << "\n";
    }
    if (priority != 0) {
        out << "\tpriority: " << priority << "\n";
    }
    if (hidden) {
        out << "\thidden";
    }
    if (!indenter.isNull()) {
        out << "\tindenter: " << indenter << "\n";
    }

    for (auto const &ctx : std::as_const(this->contexts)) {
        ctx->printDescription(out);
    }
}

int Language::highlightBlock(QTextBlock block, QVector<QTextLayout::FormatRange> &formats) {
    return highlightBlock(block, block.text(), formats);
}

int Language::highlightBlock(QTextBlock block, const QString &text,
                             QVector<QTextLayout::FormatRange> &formats) {
    auto contextStack = getContextStack(block);
    TextToMatch textToMatch(text, contextStack.currentData());

    auto *data = static_cast<TextBlockUserData *>(block.userData());
    if (!data) {
        data = new TextBlockUserData(QString(), contextStack);
        block.setUserData(data);
    }

    /* Filled in place rather than through local copies: these are per-block
     * buffers which keep their capacity from the previous highlighting pass. */
    data->textTypeMap.fill(QLatin1Char(' '), textToMatch.textLength);
    data->languageMap.fill(nullptr, textToMatch.textLength);

    QTextBlock prevBlock = block.previous();
    if (prevBlock.isValid()) {
        TextBlockUserData *prevData = static_cast<TextBlockUserData *>(prevBlock.userData());
        if (prevData) {
            data->regions = prevData->regions;
        }
    }
    data->folding.level = data->regions.size();

    auto lineContinue = false;
    do {
        auto const *context = contextStack.currentContext();
        context->parseBlock(contextStack, textToMatch, formats, data->textTypeMap,
                            data->languageMap, lineContinue, data);
    } while (!textToMatch.isEmpty());

    if (!lineContinue) {
        switchAtEndOfLine(contextStack);
    }

    data->contexts = contextStack;

    size_t regionsHash = 0;
    for (const auto &region : std::as_const(data->regions)) {
        regionsHash = qHash(region, regionsHash);
    }

    return static_cast<int>((qHash(contextStack) ^ regionsHash));
}

ContextPtr Language::getContext(const QString &contextName) const {
    auto it = std::find_if(contexts.begin(), contexts.end(), [&contextName](const ContextPtr &ctx) {
        return ctx->name() == contextName;
    });

    if (it != contexts.end()) {
        return *it;
    }
    return ContextPtr();
}

void Language::setTheme(const Theme *theme) {
    for (auto &ctx : contexts) {
        ctx->setTheme(theme);
    }
}

ContextStack Language::getContextStack(QTextBlock block) {
    TextBlockUserData *data = nullptr;

    QTextBlock prevBlock = block.previous();
    if (prevBlock.isValid()) {
        data = static_cast<TextBlockUserData *>(prevBlock.userData());
    }

    if (data != nullptr) {
        return data->contexts;
    } else {
        return defaultContextStack;
    }
}

void Language::switchAtEndOfLine(ContextStack &contextStack) {
    while (!contextStack.currentContext()->lineEndContext().isNull()) {
        auto const *previousContext = contextStack.currentContext();
        auto const previousDepth = contextStack.depth();
        contextStack.switchTo(contextStack.currentContext()->lineEndContext());
        if (contextStack.currentContext() == previousContext &&
            contextStack.depth() == previousDepth) {
            // avoid infinite while loop if nothing to switch
            break;
        }
    }

    // this code is not tested, because lineBeginContext is not defined by any xml file
    if (!contextStack.currentContext()->lineBeginContext().isNull()) {
        contextStack.switchTo(contextStack.currentContext()->lineBeginContext());
    }
}

} // namespace Qutepart
