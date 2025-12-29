/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <QSharedPointer>
#include <QTextCharFormat>

namespace Qutepart {

class Theme;

class Style {
  public:
    Style();
    Style(const QString &defStyleName, QSharedPointer<QTextCharFormat> format);

    /* Called by some clients.
       If the style knows attribute it can better detect textType
     */
    void updateTextType(const QString &attribute);

    inline char textType() const { return _textType; }
    inline const QStringView getDefStyle() const { return defStyleName; }
    inline const QSharedPointer<QTextCharFormat> format() const { return displayFormat; }

    void setTheme(const Theme *newTheme);
    inline const Theme *getTheme() const { return theme; }

  private:
    QSharedPointer<QTextCharFormat> savedFormat;
    QSharedPointer<QTextCharFormat> displayFormat;
    char _textType;

    QString defStyleName;
    const Theme *theme = nullptr;
};

Style makeStyle(const QString &defStyleName, const QString &color, const QString & /*selColor*/,
                const QHash<QString, bool> &flags, QString &error);

} // namespace Qutepart
