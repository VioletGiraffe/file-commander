/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <QStringList>

#include "context.h"
#include "style.h"

namespace Qutepart {

class AbstractRule;

/* The outcome of a successful rule match.
 *
 * A single instance is reused for every match attempt of a whole document, so
 * everything here that belongs to the matched rule is referenced rather than
 * copied - a Style copy alone costs two atomic reference counts, and there is
 * one match per token.  The pointees are owned by the rule, which outlives the
 * highlighting pass.
 */
class MatchResult {
  public:
    MatchResult();

    int length;
    QStringList data; // capture groups; only filled when the target context is dynamic
    bool lineContinue;
    const ContextSwitcher *nextContext;
    const Style *style;
    const AbstractRule *rule;
};

} // namespace Qutepart
