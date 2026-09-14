/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "text_block_user_data.h"

namespace Qutepart {

TextBlockUserData::TextBlockUserData(const QString &textTypeMap, const ContextStack &contexts)
    : textTypeMap(textTypeMap), contexts(contexts) {}

} // namespace Qutepart
