/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <QSharedPointer>
#include <QXmlStreamReader>

#include "language.h"

namespace Qutepart {

QSharedPointer<Language> loadLanguage(const QString &xmlFileName);

ContextPtr loadExternalContext(const QString &contextName);

} // namespace Qutepart
