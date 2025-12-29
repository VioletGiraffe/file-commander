/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include <QDebug>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QSharedPointer>

#include "qutepart.h"
#include "rules.h"
#include "style.h"

#include "loader.h"

namespace Qutepart {

const QString DEFAULT_DELIMINATOR = " \t.():!+,-<=>%&*/;?[]^{|}~\\";

QMap<QString, QSharedPointer<Language>> loadedLanguageCache;
QMutex loadedLanguageCacheLock;

QList<RulePtr> loadRules(QXmlStreamReader &xmlReader, QString &error);

QHash<QString, QString> attrsToInsensitiveHashMap(const QXmlStreamAttributes &attrs) {
    QHash<QString, QString> result;
    for (auto const &attr : std::as_const(attrs)) {
        result[attr.name().toString().toLower()] = attr.value().toString();
    }

    return result;
}

QString getAttribute(QXmlStreamAttributes attrs, QString name, QString defaultValue = QString(),
                     bool warnIfNotSet = false) {
    if (attrs.hasAttribute(name)) {
        return attrs.value(name).toString();
    } else {
        if (warnIfNotSet) {
            qWarning() << "Required attribute " << name << " is not set";
        }
        return defaultValue;
    }
}

QString getRequiredAttribute(QXmlStreamAttributes attrs, QString name, QString &error) {
    if (attrs.hasAttribute(name)) {
        return attrs.value(name).toString();
    } else {
        error = QString("Required attribute %1 is not set").arg(name);
        return QString();
    }
}

bool parseBoolAttribute(const QString &value, QString &error) {
    QString lowerValue = value.toLower();

    if (lowerValue == "true" || lowerValue == "1") {
        return true;
    } else if (lowerValue == "false" || lowerValue == "0") {
        return false;
    } else {
        error = QString("Invalid bool attribute value '%1'").arg(value);
        return false;
    }
}

int parseIntAttribute(const QString &value, QString &error) {
    bool intOk = true;
    int result = value.toInt(&intOk);
    if (!intOk) {
        error = QString("Bad integer value '%1'").arg(value);
    }

    return result;
}

QString processEscapeSequences(QString value) {
    value.replace("\\a", "\a");
    value.replace("\\b", "\b");
    value.replace("\\f", "\f");
    value.replace("\\n", "\n");
    value.replace("\\r", "\r");
    value.replace("\\t", "\t");
    value.replace("\\\\", "\\");

    return value;
}

ContextSwitcher makeContextSwitcher(QString contextOperation) {
    int popsCount = 0;
    QString contextToSwitch;

    QString rest = contextOperation;
    while (rest.startsWith("#pop")) {
        popsCount += 1;
        rest = rest.remove(0, 4); // 4 is length of '#pop'
        if (rest.startsWith("!")) {
            rest = rest.remove(0, 1); // remove !
        }
    }

    if (rest == "#stay") {
        if (popsCount > 0) {
            qWarning() << QString("Invalid context operation '%1'").arg(contextOperation);
        }
    } else {
        contextToSwitch = rest;
    }

    if (popsCount > 0 || (!contextToSwitch.isNull())) {
        return ContextSwitcher(popsCount, contextToSwitch, contextOperation);
    } else {
        return ContextSwitcher();
    }
}

AbstractRuleParams parseAbstractRuleParams(const QXmlStreamAttributes &attrs, QString &error) {
    QString attribute = getAttribute(attrs, "attribute").toLower();

    QString contextText = getAttribute(attrs, "context", "#stay");
    ContextSwitcher context = makeContextSwitcher(contextText);

    bool lookAhead = parseBoolAttribute(getAttribute(attrs, "lookAhead", "false"), error);
    if (!error.isNull()) {
        error = QString("Failed to parse 'lookAhead': %1").arg(error);
        return AbstractRuleParams();
    }

    bool firstNonSpace = parseBoolAttribute(getAttribute(attrs, "firstNonSpace", "false"), error);
    if (!error.isNull()) {
        error = QString("Failed to parse 'firstNonSpace': %1").arg(error);
        return AbstractRuleParams();
    }

    bool dynamic = parseBoolAttribute(getAttribute(attrs, "dynamic", "false"), error);
    if (!error.isNull()) {
        error = QString("Failed to parse 'dynamic': %1").arg(error);
        return AbstractRuleParams();
    }

    auto beginRegion = getAttribute(attrs, "beginRegion");
    auto endRegion = getAttribute(attrs, "endRegion");
    auto column = -1;
    auto columnStr = getAttribute(attrs, "column");
    if (!columnStr.isNull()) {
        column = parseIntAttribute(columnStr, error);
        if (!error.isNull()) {
            error = QString("Bad integer column value: %1").arg(error);
            return AbstractRuleParams();
        }
    }

    return AbstractRuleParams{attribute, context, lookAhead,   firstNonSpace,
                              column,    dynamic, beginRegion, endRegion};
}

template <class RuleClass>
RuleClass *loadStringRule(const QXmlStreamAttributes &attrs, const AbstractRuleParams &params,
                          QString &error) {
    QString value = getRequiredAttribute(attrs, "String", error);
    if (!error.isNull()) {
        return nullptr;
    }

    bool insensitive = parseBoolAttribute(getAttribute(attrs, "insensitive", "false"), error);
    if (!error.isNull()) {
        error = QString("Failed to parse 'insensitive': %1").arg(error);
        return nullptr;
    }

    return new RuleClass(params, value, insensitive);
}

KeywordRule *loadKeywordRule(const QXmlStreamAttributes &attrs, const AbstractRuleParams &params,
                             QString &error) {
    QString listName = getRequiredAttribute(attrs, "String", error);
    if (!error.isNull()) {
        return nullptr;
    }

    return new KeywordRule(params, listName);
}

DetectCharRule *loadDetectChar(const QXmlStreamAttributes &attrs, const AbstractRuleParams &params,
                               QString &error) {
    QString strValue = getRequiredAttribute(attrs, "char", error);
    if (!error.isNull()) {
        return nullptr;
    }

    strValue = processEscapeSequences(strValue);
    QChar value = '\0';

    int index = 0;

    if (params.dynamic) {
        bool ok = false;
        index = strValue.toInt(&ok);
        if (!ok) {
            error = QString("Bad integer value: %1").arg(strValue);
            return nullptr;
        }

        if (index < 0) {
            error = QString("Bad integer value: %1").arg(strValue);
            return nullptr;
        }
    } else {
        value = strValue[0];
    }

    return new DetectCharRule(params, value, index);
}

Detect2CharsRule *loadDetect2Chars(const QXmlStreamAttributes &attrs,
                                   const AbstractRuleParams &params, QString &error) {
    QString char0 = getRequiredAttribute(attrs, "char", error);
    if (!error.isNull()) {
        return nullptr;
    }
    QString char1 = getRequiredAttribute(attrs, "char1", error);
    if (!error.isNull()) {
        return nullptr;
    }

    QString value = processEscapeSequences(char0) + processEscapeSequences(char1);

    return new Detect2CharsRule(params, value, false);
}

RegExpRule *loadRegExp(const QXmlStreamAttributes &attrs, const AbstractRuleParams &params,
                       QString &error) {
    QString value = getRequiredAttribute(attrs, "String", error);
    if (!error.isNull()) {
        return nullptr;
    }

    bool insensitive = parseBoolAttribute(getAttribute(attrs, "insensitive", "false"), error);
    if (!error.isNull()) {
        error = QString("Failed to parse 'insensitive': %1").arg(error);
        return nullptr;
    }

    bool minimal = parseBoolAttribute(getAttribute(attrs, "minimal", "false"), error);
    if (!error.isNull()) {
        error = QString("Failed to parse 'minimal': %1").arg(error);
        return nullptr;
    }

    bool wordStart = false;
    bool lineStart = false;

    if (!value.isEmpty()) {
        QStringView strippedValue = value.mid(0);
        while (strippedValue.startsWith('(')) {
            strippedValue = strippedValue.mid(1);
        }

        if (!(strippedValue.length() > 1 &&
              strippedValue.at(1) == '|')) { // ^|blabla   This condition is not ideal but
                                             // will cover majority of cases
            wordStart = strippedValue.startsWith(QLatin1String("\\b"));
            lineStart = strippedValue.startsWith('^');
        }
    }

    return new RegExpRule(params, value, insensitive, minimal, wordStart, lineStart);
}

template <class RuleClass>
RuleClass *loadNumberRule(QXmlStreamReader &xmlReader, const AbstractRuleParams &params,
                          QString &error) {
    QList<RulePtr> children = loadRules(xmlReader, error);

    if (!error.isNull()) {
        error = QString("Failed to load child rules of number: %1").arg(error);
        return nullptr;
    }

    return new RuleClass(params, children);
}

RangeDetectRule *loadRangeDetectRule(const QXmlStreamAttributes &attrs,
                                     const AbstractRuleParams &params, QString &error) {
    QString char0 = getRequiredAttribute(attrs, "char", error);
    if (!error.isNull()) {
        return nullptr;
    }
    QString char1 = getRequiredAttribute(attrs, "char1", error);
    if (!error.isNull()) {
        return nullptr;
    }

    return new RangeDetectRule(params, char0, char1);
}

IncludeRulesRule *loadIncludeRulesRule(const QXmlStreamAttributes &attrs,
                                       const AbstractRuleParams &params, QString &error) {
    QString contextName = getRequiredAttribute(attrs, "context", error);
    if (!error.isNull()) {
        return nullptr;
    }

    return new IncludeRulesRule(params, contextName);
}

AbstractRule *loadRule(QXmlStreamReader &xmlReader, QString &error) {
    QXmlStreamAttributes attrs = xmlReader.attributes();

    AbstractRuleParams params = parseAbstractRuleParams(attrs, error);
    if (!error.isNull()) {
        return nullptr;
    }

    QStringView name = xmlReader.name();

    AbstractRule *result = nullptr;
    if (name == QLatin1String("keyword")) {
        result = loadKeywordRule(attrs, params, error);
    } else if (name == QLatin1String("DetectChar")) {
        result = loadDetectChar(attrs, params, error);
    } else if (name == QLatin1String("Detect2Chars")) {
        result = loadDetect2Chars(attrs, params, error);
    } else if (name == QLatin1String("AnyChar")) {
        result = loadStringRule<AnyCharRule>(attrs, params, error);
    } else if (name == QLatin1String("StringDetect")) {
        result = loadStringRule<StringDetectRule>(attrs, params, error);
    } else if (name == QLatin1String("WordDetect")) {
        result = loadStringRule<WordDetectRule>(attrs, params, error);
    } else if (name == QLatin1String("RegExpr")) {
        result = loadRegExp(attrs, params, error);
    } else if (name == QLatin1String("Int")) {
        result = loadNumberRule<IntRule>(xmlReader, params, error);
    } else if (name == QLatin1String("Float")) {
        result = loadNumberRule<FloatRule>(xmlReader, params, error);
    } else if (name == QLatin1String("HlCHex")) {
        result = new HlCHexRule(params);
    } else if (name == QLatin1String("HlCOct")) {
        result = new HlCOctRule(params);
    } else if (name == QLatin1String("HlCStringChar")) {
        result = new HlCStringCharRule(params);
    } else if (name == QLatin1String("HlCChar")) {
        result = new HlCCharRule(params);
    } else if (name == QLatin1String("RangeDetect")) {
        result = loadRangeDetectRule(attrs, params, error);
    } else if (name == QLatin1String("LineContinue")) {
        result = new LineContinueRule(params);
    } else if (name == QLatin1String("IncludeRules")) {
        result = loadIncludeRulesRule(attrs, params, error);
    } else if (name == QLatin1String("DetectSpaces")) {
        result = new DetectSpacesRule(params);
    } else if (name == QLatin1String("DetectIdentifier")) {
        result = new DetectIdentifierRule(params);
    } else {
        error = QString("Unknown rule %1").arg(name.toString());
        return nullptr;
    }

    if (!error.isNull()) {
        return nullptr;
    }

    if (!xmlReader.isEndElement()) {
        xmlReader.readNextStartElement();
    }

    return result;
}

QList<RulePtr> loadRules(QXmlStreamReader &xmlReader, QString &error) {
    QList<RulePtr> rules;

    while (xmlReader.readNextStartElement()) {
        AbstractRule *rule = loadRule(xmlReader, error);
        if (!error.isNull()) {
            break;
        }
        rules.append(RulePtr(rule));
    }

    return rules;
}

Context *loadContext(QXmlStreamReader &xmlReader, QString &error) {
    QXmlStreamAttributes attrs = xmlReader.attributes();

    QString name = getRequiredAttribute(attrs, "name", error);
    if (!error.isNull()) {
        return nullptr;
    }

    QString attribute = getRequiredAttribute(attrs, "attribute", error).toLower();
    if (!error.isNull()) {
        return nullptr;
    }

    QString lineEndContextText = getAttribute(attrs, "lineEndContext", "#stay");
    ContextSwitcher lineEndContext = makeContextSwitcher(lineEndContextText);
    QString lineBeginContextText = getAttribute(attrs, "lineEndContext", "#stay");
    ContextSwitcher lineBeginContext = makeContextSwitcher(lineBeginContextText);
    QString lineEmptyContextText = getAttribute(attrs, "lineEmptyContext", "#stay");
    ContextSwitcher lineEmptyContext = makeContextSwitcher(lineEmptyContextText);

    bool fallthrough = parseBoolAttribute(getAttribute(attrs, "fallthrough", "false"), error);
    if (!error.isNull()) {
        error = QString("Failed to parse 'fallthrough': %1").arg(error);
        return nullptr;
    }

    ContextSwitcher fallthroughContext;

    if (fallthrough) {
        QString fallthroughContextText = getAttribute(attrs, "fallthroughContext", "#stay", true);
        fallthroughContext = makeContextSwitcher(fallthroughContextText);
    }

    bool dynamic = parseBoolAttribute(getAttribute(attrs, "dynamic", "false"), error);
    if (!error.isNull()) {
        error = QString("Failed to parse 'dynamic': %1").arg(error);
    }

    QList<RulePtr> rules = loadRules(xmlReader, error);
    if (!error.isNull()) {
        error = QString("Failed to parse context %1: %2").arg(name).arg(error);
        return nullptr;
    }

    return new Context(name, attribute, lineEndContext, lineBeginContext, lineEmptyContext,
                       fallthroughContext, dynamic, rules);
}

QList<ContextPtr> loadContexts(QXmlStreamReader &xmlReader, QString &error) {
    if (xmlReader.name() != QLatin1String("contexts")) {
        error = QString("<contexts> tag not found. Found <%1>").arg(xmlReader.name().toString());
        return QList<ContextPtr>();
    }

    QList<ContextPtr> contexts; // result
    while (xmlReader.readNextStartElement()) {
        if (xmlReader.name() != QLatin1String("context")) {
            error = QString("Not expected tag when parsing contexts <%1>")
                        .arg(xmlReader.name().toString());
            return QList<ContextPtr>();
        }

        Context *ctx = loadContext(xmlReader, error);
        if (ctx == nullptr) {
            return QList<ContextPtr>();
        }

        ContextPtr ctxPtr = ContextPtr(ctx);
        contexts.append(ctxPtr);
    }

    return contexts;
}

QStringList loadKeywordList(QXmlStreamReader &xmlReader, QString &error) {
    QStringList list;
    while (xmlReader.readNextStartElement()) {
        if (xmlReader.name() == QLatin1String("include")) {
            // TODO includes in list is not supported yet
            xmlReader.readElementText();
            qDebug()
                << "include list is not supported yet, some parts of language will not be loaded"
                << xmlReader.device();
            continue;
        }
        if (xmlReader.name() != QLatin1String("item")) {
            auto n = xmlReader.name().toString();
            error = QString("Not expected tag <%1> when loading list").arg(n);
            return QStringList();
        }
        list << xmlReader.readElementText();
    }

    return list;
}

QHash<QString, QStringList> loadKeywordLists(QXmlStreamReader &xmlReader, QString &error) {
    QHash<QString, QStringList> lists;

    while (xmlReader.readNextStartElement()) {
        if (xmlReader.name() != QLatin1String("list")) {
            return lists;
        }

        QXmlStreamAttributes attrs = xmlReader.attributes();

        QString name = getRequiredAttribute(attrs, "name", error);
        if (!error.isNull()) {
            return QHash<QString, QStringList>();
        }

        QStringList list = loadKeywordList(xmlReader, error);
        if (!error.isNull()) {
            return QHash<QString, QStringList>();
        }

        lists[name] = list;
    }

    return lists;
}

QHash<QString, Style> loadStyles(QXmlStreamReader &xmlReader, QString &error) {
    xmlReader.readNextStartElement();

    if (xmlReader.name() != QLatin1String("itemDatas")) {
        error = QString("<itemDatas> tag not found. Found <%1>").arg(xmlReader.name().toString());
        return QHash<QString, Style>();
    }

    QHash<QString, Style> styles;

    while (xmlReader.readNextStartElement()) {
        if (xmlReader.name() != QLatin1String("itemData")) {
            error = QString("Not expected tag when parsing itemDatas <%1>")
                        .arg(xmlReader.name().toString());
            return QHash<QString, Style>();
        }

        QXmlStreamAttributes attrs = xmlReader.attributes();

        QString name = getRequiredAttribute(attrs, "name", error);
        if (!error.isNull()) {
            return QHash<QString, Style>();
        }

        QString defStyleNum = getRequiredAttribute(attrs, "defStyleNum", error);
        if (!error.isNull()) {
            return QHash<QString, Style>();
        }

        QString color = getAttribute(attrs, "color");
        QString selColor = getAttribute(attrs, "selColor");

        QHash<QString, QString> attrsMap = attrsToInsensitiveHashMap(attrs);
        QHash<QString, bool> flags;
        foreach (const auto &flagName, QStringList() << "spellChecking"
                                                     << "italic"
                                                     << "bold"
                                                     << "underline"
                                                     << "strikeout") {
            if (attrsMap.contains(flagName)) {
                bool val = parseBoolAttribute(attrsMap.value(flagName, "false"), error);
                if (!error.isNull()) {
                    error = QString("Failed to parse 'spellChecking' of itemData: %1").arg(error);
                    return QHash<QString, Style>();
                }
                flags[flagName] = val;
            }
        }

        Style style = makeStyle(defStyleNum, color, selColor, flags, error);
        if (!error.isNull()) {
            return QHash<QString, Style>();
        }

        styles[name.toLower()] = style;
        xmlReader.readNextStartElement();
    }

    // HACK not documented, but 'normal' attribute is used by some parsers
    // without declaration
    if (!styles.contains("normal")) {
        styles["normal"] = makeStyle("dsNormal", {}, {}, {}, error);
    }
    if (!styles.contains("string")) {
        styles["string"] = makeStyle("dsString", {}, {}, {}, error);
    }

    return styles;
}

auto static loadComments(QXmlStreamReader &xmlReader, QString &start, QString &end,
                         QString &singleLine, QString &error) -> QString {
    while (xmlReader.readNextStartElement()) {
        if (xmlReader.name() != QLatin1String("comment")) {
            error = QString("Not expected tag when parsing comments <%1>")
                        .arg(xmlReader.name().toString());
            return {};
        }
        auto attrs = xmlReader.attributes();
        auto name = getRequiredAttribute(attrs, "name", error);
        if (!error.isNull()) {
            return {};
        }

        if (name == QStringLiteral("singleLine")) {
            singleLine = getRequiredAttribute(attrs, "start", error);
        } else if (name == QStringLiteral("multiLine")) {
            start = getRequiredAttribute(attrs, "start", error);
            end = getAttribute(attrs, "end", error);
        } else {
            error = QString("Invalid comment type <%1>").arg(name);
            return {};
        }
        if (!error.isNull()) {
            return {};
        }

        xmlReader.readNextStartElement();
    }

    return "ok";
}

QSet<QChar> strToSet(const QString &str) {
    QSet<QChar> res;
    for (auto chr : str) {
        res.insert(chr);
    }
    return res;
}

QString setToStr(const QSet<QChar> &set) {
    QString res;
    for (auto chr : set) {
        res += chr;
    }
    return res;
}

void loadKeywordParams(const QXmlStreamAttributes &attrs, QString &keywordDeliminators,
                       bool &keywordsKeySensitive, QString &error) {
    if (attrs.hasAttribute("casesensitive")) {
        keywordsKeySensitive = parseBoolAttribute(getAttribute(attrs, "casesensitive"), error);
        if (!error.isNull()) {
            return;
        }
    }

    QSet<QChar> deliminatorSet = strToSet(keywordDeliminators);

    if (attrs.hasAttribute("weakDeliminator")) {
        QSet<QChar> weakSet = strToSet(getAttribute(attrs, "weakDeliminator"));
        deliminatorSet.subtract(weakSet);
    }

    if (attrs.hasAttribute("additionalDeliminator")) {
        QSet<QChar> additionalSet = strToSet(getAttribute(attrs, "additionalDeliminator"));
        deliminatorSet.unite(additionalSet);
    }

    keywordDeliminators = setToStr(deliminatorSet);
}

void makeKeywordsLowerCase(QHash<QString, QStringList> &keywordLists) {
    for (auto &list : keywordLists) {
        for (int i = 0; i < list.size(); i++) {
            list[i] = list[i].toLower();
        }
    }
}

// Load keyword lists, contexts, attributes
QList<ContextPtr> loadLanguageSytnax(QXmlStreamReader &xmlReader, QString &keywordDeliminators,
                                     QString &indenter, QSet<QString> &allLanguageKeywords,
                                     QString &start, QString &end, QString &singleLine,
                                     QString &error) {
    QHash<QString, QStringList> keywordLists = loadKeywordLists(xmlReader, error);
    if (!error.isNull()) {
        return QList<ContextPtr>();
    }

    QList<ContextPtr> contexts = loadContexts(xmlReader, error);
    if (!error.isNull()) {
        return QList<ContextPtr>();
    }

    QHash<QString, Style> styles = loadStyles(xmlReader, error);
    if (!error.isNull()) {
        return QList<ContextPtr>();
    }

    bool keywordsKeySensitive = true;
    keywordDeliminators = DEFAULT_DELIMINATOR;

    while (!xmlReader.atEnd()) {
        xmlReader.readNextStartElement();

        if (xmlReader.name() == QLatin1String("keywords")) {
            loadKeywordParams(xmlReader.attributes(), keywordDeliminators, keywordsKeySensitive,
                              error);
            if (!error.isNull()) {
                return QList<ContextPtr>();
            }

            // Convert all list items to lowercase
            if (!keywordsKeySensitive) {
                makeKeywordsLowerCase(keywordLists);
            }
        } else if (xmlReader.name() == QLatin1String("comments")) {
            loadComments(xmlReader, start, end, singleLine, error);
            if (!error.isNull()) {
                return {};
            }
        } else if (xmlReader.name() == QLatin1String("indentation")) {
            if (xmlReader.attributes().hasAttribute("mode")) {
                indenter = getAttribute(xmlReader.attributes(), "mode", QString());
            }
        }
    }

    for (auto &context : contexts) {
        context->setKeywordParams(keywordLists, keywordDeliminators, keywordsKeySensitive, error);
        if (!error.isNull()) {
            return QList<ContextPtr>();
        }

        context->setStyles(styles, error);
        if (!error.isNull()) {
            return QList<ContextPtr>();
        }
    }

    for (auto const &kwList : std::as_const(keywordLists)) {
        for (auto const &word : std::as_const(kwList)) {
            allLanguageKeywords += word;
        }
    }

    return contexts;
}

QSharedPointer<Language> parseXmlFile(const QString &xmlFileName, QXmlStreamReader &xmlReader,
                                      QString &error) {
    if (!xmlReader.readNextStartElement()) {
        error = "Failed to read start element";
        return QSharedPointer<Language>();
    }

    if (xmlReader.name() != QLatin1String("language")) {
        error = "'name' attribute not found in <language>";
        return QSharedPointer<Language>();
    }

    QXmlStreamAttributes attrs = xmlReader.attributes();

    QString name = getRequiredAttribute(attrs, "name", error);
    if (!error.isNull()) {
        return QSharedPointer<Language>();
    }

    QString extensionsStr = getRequiredAttribute(attrs, "extensions", error);
    if (!error.isNull()) {
        return QSharedPointer<Language>();
    }

    QString mimetypesStr = getAttribute(attrs, "mimetypes", QString());
    QString priorityStr = getAttribute(attrs, "priority", QString());
    QString hiddenStr = getAttribute(attrs, "hidden", "false");
    QString indenter = getAttribute(attrs, "indenter", QString());

    int priority = 0;
    if (!priorityStr.isNull()) {
        priority = parseIntAttribute(priorityStr, error);
        if (!error.isNull()) {
            error = QString("Bad integer priority value: %1").arg(error);
            return QSharedPointer<Language>();
        }
    }

    bool hidden = parseBoolAttribute(hiddenStr, error);
    if (!error.isNull()) {
        error = QString("Failed to parse 'hidden' attribute: %1").arg(error);
        return QSharedPointer<Language>();
    }

    QStringList extensions = extensionsStr.split(';', Qt::SplitBehaviorFlags::SkipEmptyParts);
    QStringList mimetypes = extensionsStr.split(';', Qt::SplitBehaviorFlags::SkipEmptyParts);

    if (!xmlReader.readNextStartElement() || xmlReader.name() != QLatin1String("highlighting")) {
        error = "<highlighting> tag not found";
        return QSharedPointer<Language>();
    }

    QString keywordDeliminators, commentStart, commentEnd, commentSingleLine;
    QSet<QString> allLanguageKeywords;
    QList<ContextPtr> contexts =
        loadLanguageSytnax(xmlReader, keywordDeliminators, indenter, allLanguageKeywords,
                           commentStart, commentEnd, commentSingleLine, error);

    if (!error.isNull()) {
        return QSharedPointer<Language>();
    }

    Language *language =
        new Language(name, extensions, mimetypes, priority, hidden, indenter, commentStart,
                     commentEnd, commentSingleLine, allLanguageKeywords, contexts);
    language->fileName = xmlFileName;
    QSharedPointer<Language> languagePtr(language);

    {
        QMutexLocker locker(&loadedLanguageCacheLock);
        loadedLanguageCache[xmlFileName] = languagePtr;
    }

    /* resolve context references only after language has been added to the map
     * to support recursive references
     */
    QHash<QString, ContextPtr> contextMap; // to resolve references
    for (auto &ctxPtr : std::as_const(contexts)) {
        contextMap[ctxPtr->name()] = ctxPtr;
    }

    for (auto &ctx : contexts) {
        ctx->resolveContextReferences(contextMap, error);
        if (!error.isNull()) {
            {
                QMutexLocker locker(&loadedLanguageCacheLock);
                loadedLanguageCache.remove(xmlFileName);
            }
            return QSharedPointer<Language>();
        }
        ctx->language = languagePtr;
    }

    return languagePtr;
}

QSharedPointer<Language> loadLanguage(const QString &xmlFileName) {
    if (xmlFileName.isEmpty()) {
        return {};
    }

    {
        QMutexLocker locker(&loadedLanguageCacheLock);
        if (loadedLanguageCache.contains(xmlFileName)) {
            return loadedLanguageCache[xmlFileName];
        }
    }

    QString xmlFilePath = ":/qutepart/syntax/" + xmlFileName;

    QFile syntaxFile(xmlFilePath);
    if (!syntaxFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCritical() << "Failed to open syntax file " << xmlFilePath;
        return QSharedPointer<Language>();
    }

    QXmlStreamReader xmlReader(&syntaxFile);

    QString error;
    QSharedPointer<Language> language = parseXmlFile(xmlFileName, xmlReader, error);
    if (language.isNull()) {
        qCritical() << "Failed to parse XML file '" << xmlFilePath << "': " << error;
        return QSharedPointer<Language>();
    }

    return language;
}

ContextPtr loadExternalContext(const QString &externalCtxName) {
    QString langName, contextName;

    if (externalCtxName.startsWith("##")) {
        langName = externalCtxName.mid(2);
    } else {
        QStringList parts = externalCtxName.split("##");
        if (parts.length() != 2) {
            // This message is not a warning because it happens often because of
            // mistakes in XMLs
            qDebug() << "Invalid external context" << externalCtxName;
            return ContextPtr();
        }
        langName = parts[1];
        contextName = parts[0];
    }

    LangInfo langInfo = chooseLanguage(QString(), langName);
    if (!langInfo.isValid()) {
        qWarning() << "Unknown language" << langName;
        return ContextPtr();
    }

    QSharedPointer<Language> language = loadLanguage(langInfo.id);
    if (language.isNull()) {
        qWarning() << "Failed to load context" << externalCtxName;
        return ContextPtr();
    }

    if (contextName.isEmpty()) {
        return language->defaultContext();
    } else {
        ContextPtr ctx = language->getContext(contextName);
        if (ctx.isNull()) {
            qWarning() << "Language" << langName << "doesn't have context" << contextName;
        }
        return ctx;
    }
}

} // namespace Qutepart
