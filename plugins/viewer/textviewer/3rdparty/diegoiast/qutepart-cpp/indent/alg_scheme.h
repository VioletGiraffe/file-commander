/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <QString>
#include <QTextBlock>

#include "indenter.h"

namespace Qutepart {

class IndentAlgScheme : public IndentAlgImpl {
  public:
    QString computeSmartIndent(QTextBlock block, int cursorPos) const override;
};

} // namespace Qutepart
