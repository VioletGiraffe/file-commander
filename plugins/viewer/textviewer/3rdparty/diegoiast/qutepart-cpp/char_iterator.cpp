/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "char_iterator.h"

namespace Qutepart {

CharIterator::CharIterator(const TextPosition &position) : position_(position) {}

QChar CharIterator::step() {
    if (!atEnd()) {
        auto s = position_.block.text();
        auto i = position_.column;
        if (i >= s.length()) {
            return QChar::Null;
        }
        QChar retVal = s[i];
        previousPosition_ = position_;
        movePosition();
        return retVal;
    } else {
        return QChar::Null;
    }
}

TextPosition CharIterator::currentPosition() const { return position_; }

TextPosition CharIterator::previousPosition() const { return previousPosition_; }

bool CharIterator::atEnd() const { return (!position_.block.isValid()); }

void ForwardCharIterator::movePosition() {
    int blockLength = position_.block.text().length();

    while (1) {
        if (position_.column < (blockLength - 1)) {
            position_.column++;
            break;
        } else {
            position_.block = position_.block.next();

            while (position_.block.isValid() && position_.block.text().isEmpty()) {
                position_.block = position_.block.next();
            }

            if (!position_.block.isValid()) {
                break;
            }

            position_.column = -1;
            /* move block backward, but the block might be empty
               Go to next while loop iteration and move back
               more blocks if necessary
             */
        }
    }
}

void BackwardCharIterator::movePosition() {
    while (1) {
        if (position_.column > 0) {
            position_.column--;
            break;
        } else {
            position_.block = position_.block.previous();
            if (!position_.block.isValid()) {
                break;
            }

            position_.column = position_.block.length() - 1;
            /* move block backward, but the block might be empty
               Go to next while loop iteration and move back
               more blocks if necessary
             */
        }
    }
}

} // namespace Qutepart
