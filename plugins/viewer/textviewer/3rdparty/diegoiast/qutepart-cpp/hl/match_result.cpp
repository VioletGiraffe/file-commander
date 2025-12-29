/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "match_result.h"

#include "rules.h"

namespace Qutepart {

MatchResult::MatchResult(int length, const QStringList &data, bool lineContinue,
                         const ContextSwitcher &context, const Style &style,
                         const AbstractRule *rule)
    : length(length), data(data), lineContinue(lineContinue), nextContext(context), style(style),
      rule(rule) {}

MatchResult::MatchResult() : length(0), lineContinue(false) {}

} // namespace Qutepart
