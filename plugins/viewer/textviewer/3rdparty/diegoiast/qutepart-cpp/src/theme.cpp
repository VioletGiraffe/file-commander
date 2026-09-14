/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include <QColor>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QTextFormat>
#include <QVector>
#include <qutepart.h>

#include <hl/rules.h>
#include <hl/syntax_highlighter.h>

#include "theme.h"

namespace Qutepart {

Theme::Theme() = default;

auto Theme::loadTheme(const QString &filename) -> bool {
    auto file = QFile(filename);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    auto jsonData = file.readAll();
    auto document = QJsonDocument::fromJson(jsonData);

    if (document.isNull() || !document.isObject()) {
        return false;
    }

    auto themeData = document.object();

    // Parse custom styles
    QJsonObject customStylesObj = themeData["custom-styles"].toObject();
    for (auto categoryIt = customStylesObj.begin(); categoryIt != customStylesObj.end();
         ++categoryIt) {
        QString category = categoryIt.key();
        QJsonObject categoryObj = categoryIt.value().toObject();

        QHash<QString, QStringHash> categoryStyles;
        for (auto styleIt = categoryObj.begin(); styleIt != categoryObj.end(); ++styleIt) {
            QString styleName = styleIt.key();
            QJsonObject styleObj = styleIt.value().toObject();

            QStringHash styleProperties;
            for (auto propIt = styleObj.begin(); propIt != styleObj.end(); ++propIt) {
                styleProperties[propIt.key()] = propIt.value().toString();
            }

            categoryStyles[styleName] = styleProperties;
        }

        customStyles[category] = categoryStyles;
    }

    // Parse editor colors
    auto editorColorsObj = themeData["editor-colors"].toObject();
    for (auto it = editorColorsObj.begin(); it != editorColorsObj.end(); ++it) {
        auto colorName = it.key();
        auto color = QColor(it.value().toString());
        editorColors[colorName] = color;
    }

    // Parse text styles
    QJsonObject textStylesObj = themeData["text-styles"].toObject();
    for (auto it = textStylesObj.begin(); it != textStylesObj.end(); ++it) {
        QString styleName = it.key();
        QJsonObject styleObj = it.value().toObject();

        QStringHash styleProperties;
        for (auto propIt = styleObj.begin(); propIt != styleObj.end(); ++propIt) {
            auto k = propIt.key();
            auto v = propIt.value().toString();
            styleProperties[k] = v;
        }

        textStyles[styleName] = styleProperties;
    }

    // Parse metadata
    auto metaDataObj = themeData["metadata"].toObject();
    auto copyrightArray = metaDataObj["copyright"].toArray();
    for (const auto &copyright : std::as_const(copyrightArray)) {
        metaData.copyright.push_back(copyright.toString());
    }
    metaData.name = metaDataObj["name"].toString();
    metaData.revision = metaDataObj["revision"].toInt();
    metaData.license = metaDataObj["license"].toString();
    metaData.description = metaDataObj["description"].toString();

    return true;
}

} // namespace Qutepart
