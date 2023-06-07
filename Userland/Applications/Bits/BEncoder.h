/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "BDecoder.h"
#include <AK/Stream.h>
#include <AK/Types.h>
namespace Bits {
class BEncoder {
public:
    static ErrorOr<void> bencode(BEncodingType, Stream&);
};
}