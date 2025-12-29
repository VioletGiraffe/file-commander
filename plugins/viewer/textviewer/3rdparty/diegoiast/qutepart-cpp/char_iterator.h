/*
 * Copyright (C) 2018-2023 Andrei Kopats
 * Copyright (C) 2023-...  Diego Iastrubni <diegoiast@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "text_pos.h"

namespace Qutepart {

class CharIterator {
  public:
    // create iterator and make first step
    CharIterator(const TextPosition &position);
    virtual ~CharIterator() = default;

    QChar step(); // return current character and then make step back
    TextPosition previousPosition() const;
    TextPosition currentPosition() const;
    bool atEnd() const;

  protected:
    TextPosition previousPosition_;
    TextPosition position_;

    virtual void movePosition() = 0;
};

class ForwardCharIterator : public CharIterator {
    using CharIterator::CharIterator;

  private:
    virtual void movePosition() override;
};

class BackwardCharIterator : public CharIterator {
    using CharIterator::CharIterator;

  private:
    virtual void movePosition() override;
};

} // namespace Qutepart
