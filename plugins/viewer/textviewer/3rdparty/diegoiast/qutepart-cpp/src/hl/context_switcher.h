/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <QHash>
#include <QSharedPointer>
#include <QString>

namespace Qutepart {

class Context;
typedef QSharedPointer<Context> ContextPtr;

class ContextSwitcher {
  public:
    ContextSwitcher();
    ContextSwitcher(int popsCount, const QString &contextName, const QString &contextOperation);

    QString toString() const;
    bool isNull() const;

    void resolveContextReferences(const QHash<QString, ContextPtr> &contexts, QString &error);

    int popsCount() const { return _popsCount; }
    ContextPtr context() const { return _context; }

  protected:
    int _popsCount;
    QString contextName;
    ContextPtr _context;
    QString contextOperation;
};

} // namespace Qutepart
