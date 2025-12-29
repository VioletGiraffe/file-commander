/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include <QDebug>

#include "indent_funcs.h"
#include "text_block_utils.h"

#include "alg_cstyle.h"
#include "alg_lisp.h"
#include "alg_python.h"
#include "alg_ruby.h"
#include "alg_scheme.h"
#include "alg_xml.h"

#include "indenter.h"

namespace Qutepart {

namespace {

class IndentAlgNone : public IndentAlgImpl {
  public:
    QString computeSmartIndent(QTextBlock /*block*/, int /*cursorPos*/) const override {
        return QString();
    }
};

class IndentAlgNormal : public IndentAlgImpl {
  public:
    QString computeSmartIndent(QTextBlock block, int /*cursorPos*/) const override {
        return prevNonEmptyBlockIndent(block);
    }
};

} // namespace

Indenter::Indenter(QObject *parent)
    : QObject(parent), alg_(new IndentAlgNormal()), useTabs_(false), width_(4) {
    alg_->setConfig(width_, useTabs_);
    alg_->setLanguage(language_);
}

Indenter::~Indenter() { delete alg_; }

void Indenter::setAlgorithm(IndentAlg alg) {
    switch (alg) {
    case INDENT_ALG_NONE:
        delete alg_;
        alg_ = new IndentAlgNone();
        break;
    case INDENT_ALG_NORMAL:
        delete alg_;
        alg_ = new IndentAlgNormal();
        break;
    case INDENT_ALG_LISP:
        delete alg_;
        alg_ = new IndentAlgLisp();
        break;
    case INDENT_ALG_XML:
        delete alg_;
        alg_ = new IndentAlgXml();
        break;
    case INDENT_ALG_SCHEME:
        delete alg_;
        alg_ = new IndentAlgScheme();
        break;
    case INDENT_ALG_PYTHON:
        delete alg_;
        alg_ = new IndentAlgPython();
        break;
    case INDENT_ALG_RUBY:
        delete alg_;
        alg_ = new IndentAlgRuby();
        break;
    case INDENT_ALG_CSTYLE:
        delete alg_;
        alg_ = new IndentAlgCstyle();
        break;
    default:
        qWarning() << "Wrong indentation algorithm requested" << alg;
        break;
    }
    alg_->setConfig(width_, useTabs_);
    alg_->setLanguage(language_);
}

QString Indenter::indentText() const { return makeIndent(width_, useTabs_); }

int Indenter::width() const { return width_; }

void Indenter::setWidth(int width) {
    width_ = width;
    alg_->setConfig(width_, useTabs_);
}

bool Indenter::useTabs() const { return useTabs_; }

void Indenter::setUseTabs(bool use) {
    useTabs_ = use;
    alg_->setConfig(width_, useTabs_);
}

void Indenter::setLanguage(const QString &language) {
    language_ = language;
    alg_->setLanguage(language_);
}

bool Indenter::shouldAutoIndentOnEvent(QKeyEvent *event) const {
    return (!event->text().isEmpty() && alg_->triggerCharacters().contains(event->text()));
}

bool Indenter::shouldUnindentWithBackspace(const QTextCursor &cursor) const {
    return textBeforeCursor(cursor).endsWith(indentText()) && (!cursor.hasSelection()) &&
           (cursor.atBlockEnd() ||
            (!cursor.block().text()[cursor.positionInBlock() + 1].isSpace()));
}

void Indenter::indentBlock(QTextBlock block, int cursorPos, int typedKey) const {
    QString prevBlockText = block.previous().text(); // invalid block returns empty text
    QString indent;
    if (typedKey == Qt::Key_Enter && prevBlockText.trimmed().isEmpty()) {
        // continue indentation, if no text
        indent = prevBlockIndent(block);

        if (!indent.isNull()) {
            QTextCursor cursor(block);
            cursor.insertText(indent);
        }
    } else {
        QString indentedLine;
        if (typedKey == 0) { // format line on shortcut
            indentedLine = alg_ ? alg_->autoFormatLine(block) : QString();
        } else {
            indentedLine = alg_ ? alg_->indentLine(block, cursorPos) : QString();
        }

        if ((!indentedLine.isNull()) && indentedLine != block.text()) {
            QTextCursor cursor(block);
            cursor.select(QTextCursor::LineUnderCursor);
            cursor.insertText(indentedLine);
        }
    }
}

// Tab pressed
void Indenter::onShortcutIndentAfterCursor(QTextCursor cursor) const {
    if (cursor.positionInBlock() == 0) { // if no any indent - indent smartly
        QTextBlock block = cursor.block();
        QString indentedLine = alg_->computeSmartIndent(block, -1);
        if (indentedLine.isEmpty()) {
            indentedLine = indentText();
        }
        cursor.insertText(indentedLine);
    } else { // have some indent, insert more
        QString indent;
        if (useTabs_) {
            indent = "\t";
        } else {
            int charsToInsert = width_ - (textBeforeCursor(cursor).length() % width_);
            indent.fill(' ', charsToInsert);
        }
        cursor.insertText(indent);
    }
}

// Backspace pressed
void Indenter::onShortcutUnindentWithBackspace(QTextCursor &cursor) const {
    int posInBlock = cursor.positionInBlock();
    if (posInBlock <= 0) {
        return;
    }

    int indentLen = indentText().length();
    int charsToRemove = posInBlock % indentLen;

    if (charsToRemove == 0) {
        charsToRemove = indentLen;
    }

    if (charsToRemove > posInBlock) {
        charsToRemove = posInBlock;
    }

    cursor.setPosition(cursor.position() - charsToRemove, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
}

} // namespace Qutepart
