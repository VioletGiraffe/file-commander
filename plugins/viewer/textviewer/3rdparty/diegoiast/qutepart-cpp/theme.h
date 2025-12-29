#pragma once

/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * \file theme.h
 * \brief Theme support for Qutepart
 *
 * See also qutepart.h for usages.
 */
#include <QColor>
#include <QHash>
#include <QJsonObject>
#include <QString>

class QSyntaxHighlighter;

namespace Qutepart {

typedef QHash<QString, QString> QStringHash;

/**
 * @brief Theme metadata.
 *
 * Each theme json file, contains some metadata (copyright, license, etc).
 */
struct ThemeMetaData {
    /// Coprright of this theme (similar to license).
    QString copyright;
    /// Copyright of this theme (similar to copyright).
    QString license;
    /// Display name foe this theme
    QString name;
    /// Description (not all themes have this, might be empty).
    QString description;
    /// Version of this theme
    int revision;
};

class Theme {
  public:
    /// Constants used in the theme for defining colors1
    struct Colors {
        static constexpr const char *BackgroundColor = "BackgroundColor";
        static constexpr const char *BracketMatching = "BracketMatching";
        static constexpr const char *CodeFolding = "CodeFolding";
        static constexpr const char *CurrentLine = "CurrentLine";
        static constexpr const char *CurrentLineNumber = "CurrentLineNumber";
        static constexpr const char *IconBorder = "IconBorder";
        static constexpr const char *IndentationLine = "IndentationLine";
        static constexpr const char *LineNumbers = "LineNumbers";
        static constexpr const char *MarkBookmark = "MarkBookmark";
        static constexpr const char *MarkBreakpointActive = "MarkBreakpointActive";
        static constexpr const char *MarkBreakpointDisabled = "MarkBreakpointDisabled";
        static constexpr const char *MarkBreakpointReached = "MarkBreakpointReached";
        static constexpr const char *MarkError = "MarkError";
        static constexpr const char *MarkExecution = "MarkExecution";
        static constexpr const char *MarkWarning = "MarkWarning";
        static constexpr const char *ModifiedLines = "ModifiedLines";
        static constexpr const char *ReplaceHighlight = "ReplaceHighlight";
        static constexpr const char *SavedLines = "SavedLines";
        static constexpr const char *SearchHighlight = "SearchHighlight";
        static constexpr const char *Separator = "Separator";
        static constexpr const char *SpellChecking = "SpellChecking";
        static constexpr const char *TabMarker = "TabMarker";
        static constexpr const char *TemplateBackground = "TemplateBackground";
        static constexpr const char *TemplateFocusedPlaceholder = "TemplateFocusedPlaceholder";
        static constexpr const char *TemplatePlaceholder = "TemplatePlaceholder";
        static constexpr const char *TemplateReadOnlyPlaceholder = "TemplateReadOnlyPlaceholder";
        static constexpr const char *TextSelection = "TextSelection";
        static constexpr const char *WordWrapMarker = "WordWrapMarker";
    };

    Theme();

    /// Loads a theme definition from a JSON file
    auto loadTheme(const QString &filename) -> bool;

    /// Get the list of colors definid in this theme, use the Colors struct as the keys
    auto inline getEditorColors() const -> const QHash<QString, QColor> & { return editorColors; }

    /// Returns a reference to the theme's meta data (name, copyright, version etc).
    auto inline getMetaData() const -> const ThemeMetaData & { return metaData; }

    /// Returns all text styles defined in this theme, text styles do not contain "df" prefix
    auto inline getTextStyles() const -> const QHash<QString, QStringHash> & { return textStyles; }

  private:
    QHash<QString, QColor> editorColors;
    QHash<QString, QHash<QString, QStringHash>> customStyles;
    QHash<QString, QStringHash> textStyles;
    ThemeMetaData metaData;
};

} // namespace Qutepart