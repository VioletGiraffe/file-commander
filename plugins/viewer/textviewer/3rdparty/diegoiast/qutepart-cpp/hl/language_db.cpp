/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include <QDebug>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QString>

#include "qutepart.h"

namespace Qutepart {

extern QMap<QString, QString> mimeTypeToXmlFileName;
extern QMap<QString, QString> languageNameToXmlFileName;
extern QMap<QString, QString> extensionToXmlFileName;
extern QMap<QString, QString> firstLineToXmlFileName;
extern QMap<QString, QString> xmlFileNameToIndenter;

/*
 * Search value in map {glob pattern: value}
 * Match string with glob pattern key
 */
QString searchInGlobMap(const QMap<QString, QString> &map, const QString &string) {
	QMap<QString, QString>::const_iterator it = map.begin();
	while (it != map.end()) {
		QString wildcardExp = QRegularExpression::wildcardToRegularExpression(it.key());
		QRegularExpression re(QRegularExpression::anchoredPattern(wildcardExp),
							  QRegularExpression::CaseInsensitiveOption);
		if (re.match(string).hasMatch()) {
			return it.value();
		}
		++it;
	}

	return QString();
}

QString chooseLanguageXmlFileName(const QString &mimeType, const QString &languageName,
								  const QString &sourceFilePath, const QString &firstLine) {
	if (!languageName.isNull()) {
		if (languageNameToXmlFileName.contains(languageName)) {
			return languageNameToXmlFileName[languageName];
		}
	}

	if (!sourceFilePath.isNull()) {
		QString fileName = QFileInfo(sourceFilePath).fileName();
		QString xmlName = searchInGlobMap(extensionToXmlFileName, fileName);
		if (!xmlName.isNull()) {
			return xmlName;
		}
	}

	if (!mimeType.isNull()) {
		if (mimeTypeToXmlFileName.contains(mimeType)) {
			return mimeTypeToXmlFileName[mimeType];
		}
	}

	if (!firstLine.isNull()) {
		QString xmlName = searchInGlobMap(firstLineToXmlFileName, firstLine);
		if (!xmlName.isNull()) {
			return xmlName;
		}
	}

	return QString();
}

IndentAlg convertIndenter(const QString &stringVal) {
	if (stringVal == "none") {
		return INDENT_ALG_NONE;
	} else if (stringVal == "normal") {
		return INDENT_ALG_NORMAL;
	} else if (stringVal == "cstyle") {
		return INDENT_ALG_CSTYLE;
	} else if (stringVal == "lisp") {
		return INDENT_ALG_LISP;
	} else if (stringVal == "scheme") {
		return INDENT_ALG_SCHEME;
	} else if (stringVal == "xml") {
		return INDENT_ALG_XML;
	} else if (stringVal == "python") {
		return INDENT_ALG_PYTHON;
	} else if (stringVal == "ruby") {
		return INDENT_ALG_RUBY;
	} else if (stringVal.isNull()) {
		return INDENT_ALG_NORMAL; // default
	} else {
		//qWarning() << "Wrong indent algorithm value in the DB" << stringVal;
		return INDENT_ALG_NORMAL; // default
	}
}

/* Choose language XML file name by available parameters
 * First parameters have higher priority
 */
LangInfo chooseLanguage(const QString &mimeType, const QString &languageName,
						const QString &sourceFilePath, const QString &firstLine) {
	QString xmlName = chooseLanguageXmlFileName(mimeType, languageName, sourceFilePath, firstLine);

	if (xmlName.isNull()) {
		return LangInfo();
	} else {
		QList<QString> langNames = languageNameToXmlFileName.keys(xmlName);
		IndentAlg indentAlg = convertIndenter(xmlFileNameToIndenter[xmlName]);
		return LangInfo(xmlName, langNames, indentAlg);
	}
}

} // namespace Qutepart
