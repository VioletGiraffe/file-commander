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

uint qHash(const ContextStackItem &key, uint seed = 0);
uint qHash(const ContextStack &key, uint seed = 0);

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
    // Apply context switch operation and return new context
    ContextStack switchContext(const ContextSwitcher &operation,
                               const QStringList &data = QStringList()) const;

    // Get current context
    const Context *currentContext() const;

    // Get current data
    const QStringList &currentData() const;

  private:
    QVector<ContextStackItem> items;

    friend uint qHash(const ContextStack &key, uint seed);
};

} // namespace Qutepart
