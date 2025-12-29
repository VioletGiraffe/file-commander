/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "indenter.h"

namespace Qutepart {

class IndentAlgLisp : public IndentAlgImpl {
  public:
    const QString &triggerCharacters() const override;
    QString computeSmartIndent(QTextBlock block, int cursorPos) const override;
};

} // namespace Qutepart
