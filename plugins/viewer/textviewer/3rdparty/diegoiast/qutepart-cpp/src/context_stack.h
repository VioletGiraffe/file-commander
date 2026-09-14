/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <QStringList>

namespace Qutepart {

class ContextSwitcher;
class Context;
struct ContextStackItem;
class ContextStack;

size_t qHash(const ContextStackItem &key, uint seed = 0);
size_t qHash(const ContextStack &key, uint seed = 0);

struct ContextStackItem {
    ContextStackItem();
    ContextStackItem(const Context *context, const QStringList &data = QStringList());

    bool operator==(const ContextStackItem &other) const;

    const Context *context;
    QStringList data;
};

class ContextStack {
  public:
    ContextStack(Context *context);

    bool operator==(const ContextStack &other) const;
    bool operator!=(const ContextStack &other) const;

  private:
    ContextStack(const QVector<ContextStackItem> &items);

  public:
    /* Apply a context switch operation in place.
     *
     * The parser throws the previous stack away on every switch, so mutating
     * beats returning a fresh copy: the underlying list keeps its capacity and
     * a whole document's worth of switches costs no allocation at all.
     */
    void switchTo(const ContextSwitcher &operation, const QStringList &data = QStringList());

    // Apply context switch operation and return new context
    ContextStack switchContext(const ContextSwitcher &operation,
                               const QStringList &data = QStringList()) const;

    // Get current context
    const Context *currentContext() const;

    // Get current data
    const QStringList &currentData() const;

    inline int depth() const { return int(items.size()); }

  private:
    QVector<ContextStackItem> items;

    friend size_t qHash(const ContextStack &key, uint seed);
};

} // namespace Qutepart
