/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <QString>
#include <QTextBlock>

namespace Qutepart {

class IndentAlgImpl {
  public:
    virtual ~IndentAlgImpl();

    void setConfig(int width, bool useTabs);
    void setLanguage(const QString &language);

    virtual const QString &triggerCharacters() const;

    /* Compute line with proper indentation
     * and other formatting.
     * i.e. split line onto multiple lines.
     * This method is called on autoindentation shortcut.
     *
     * Default implementation indents the line with computeSmartIndent()

     * Return value QString::null means 'do not modify the string'
     */
    virtual QString autoFormatLine(QTextBlock block) const;

    /* Indent line after Enter or trigger character pressed
     * Default implementation calls computeSmartIndent()
     *
     * Return whole line contents
     */
    virtual QString indentLine(QTextBlock block, int cursorPos) const;

    /* Compute indent for line.
     * The majority of algorithms should override this method.
     *
     * cursorPos is -1 if autoIndent shortcut is triggered,
     * actual position if text is being typed
     *
     * This method is called by default implementation of autoFormatLine()
     *
     * Default implementation returns empty string
     */
    virtual QString computeSmartIndent(QTextBlock block, int cursorPos) const;

  protected:
    int width_;
    bool useTabs_;
    QString language_;

    QString indentText() const;
};

} // namespace Qutepart
