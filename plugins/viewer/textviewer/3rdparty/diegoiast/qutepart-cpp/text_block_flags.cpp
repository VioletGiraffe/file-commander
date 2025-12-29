/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "text_block_flags.h"
#include "text_block_user_data.h"
#include <qutepart.h>

namespace Qutepart {

bool hasFlag(const QTextBlock &block, int flag) {
    auto data = static_cast<TextBlockUserData *>(block.userData());
    if (!data) {
        return false;
    }
    auto state = data->state;
    return state != -1 && state & flag;
}

void setFlag(QTextBlock &block, int flag, bool value) {
    auto data = static_cast<TextBlockUserData *>(block.userData());
    if (!data) {
        return;
    }
    auto &state = data->state;
    if (state == -1) {
        state = 0;
    }

    if (value) {
        state |= flag;
    } else {
        state &= (~flag);
    }
}

} // namespace Qutepart
