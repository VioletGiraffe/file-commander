/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "match_result.h"

#include "rules.h"

namespace Qutepart {

MatchResult::MatchResult()
    : length(0), lineContinue(false), nextContext(nullptr), style(nullptr), rule(nullptr) {}

} // namespace Qutepart
