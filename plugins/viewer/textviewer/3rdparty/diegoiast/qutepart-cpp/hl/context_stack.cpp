/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include <QDebug>
#include <QHashFunctions>

#include "context.h"
#include "context_switcher.h"

#include "context_stack.h"

#define VERBOSE_LOGS 0

namespace Qutepart {
// FIXME avoid data where possible

ContextStackItem::ContextStackItem() : context(nullptr) {}

ContextStackItem::ContextStackItem(const Context *context, const QStringList &data)
    : context(context), data(data) {}

bool ContextStackItem::operator==(const ContextStackItem &other) const {
    return context == other.context && data == other.data;
}

ContextStack::ContextStack(Context *context) { items.append(ContextStackItem(context)); }

ContextStack::ContextStack(const QVector<ContextStackItem> &items) : items(items) {}

bool ContextStack::operator==(const ContextStack &other) const { return items == other.items; }

const Context *ContextStack::currentContext() const { return items.last().context; }

const QStringList &ContextStack::currentData() const { return items.last().data; }

ContextStack ContextStack::switchContext(const ContextSwitcher &operation,
                                         const QStringList &data) const {
    auto newItems = items;

    if (operation.popsCount() > 0) {
        if (newItems.size() - 1 < operation.popsCount()) {
#if VERBOSE_LOGS
            qWarning() << "#pop value is too big " << newItems.size() << operation.popsCount();
#endif

            if (newItems.size() > 1) {
                newItems = newItems.mid(0, 1);
            }
        } else {
            newItems = newItems.mid(0, newItems.size() - operation.popsCount());
        }
    }

    if (!operation.context().isNull()) {
        newItems.append(ContextStackItem(operation.context().data(), data));
    }

    return ContextStack(newItems);
}

bool ContextStack::operator!=(const ContextStack &other) const { return !(*this == other); }

uint qHash(const ContextStackItem &key, uint seed) {
    return ::qHash(key.context, seed) ^ ::qHash(key.data, seed);
}

uint qHash(const ContextStack &key, uint seed) {
    return qHashRange(key.items.begin(), key.items.end(), seed);
}

} // namespace Qutepart
