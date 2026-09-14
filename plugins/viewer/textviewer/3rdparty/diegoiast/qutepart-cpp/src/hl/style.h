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

    /* The format without touching the shared pointer's reference count. The
     * highlighter asks for it once per matched token, where a pair of atomic
     * increments is measurable. The pointee outlives every match: it is owned
     * by the Style, which is owned by the rule or context being matched.
     */
    inline const QTextCharFormat *formatData() const { return displayFormat.data(); }

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
