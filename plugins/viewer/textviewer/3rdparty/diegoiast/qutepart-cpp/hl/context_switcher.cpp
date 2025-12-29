/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "loader.h"

#include "context_switcher.h"

namespace Qutepart {

ContextSwitcher::ContextSwitcher() : _popsCount(0) {}

ContextSwitcher::ContextSwitcher(int popsCount, const QString &contextName,
                                 const QString &contextOperation)
    : _popsCount(popsCount), contextName(contextName), contextOperation(contextOperation) {}

QString ContextSwitcher::toString() const { return contextOperation; }

bool ContextSwitcher::isNull() const { return contextOperation.isEmpty(); }

void ContextSwitcher::resolveContextReferences(const QHash<QString, ContextPtr> &contexts,
                                               QString &error) {
    if (contextName.isEmpty()) {
        return;
    }

    if (contextName.contains('#')) {
        _context = loadExternalContext(contextName);
        return;
    }

    if (!contexts.contains(contextName)) {
        error = QString("Failed to get context '%1'").arg(contextName);
        return;
    }

    _context = contexts[contextName];
}

} // namespace Qutepart
