/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "AK/Types.h"
#include "BDecoder.h"
#include <AK/Stream.h>

class BEncoder {
public:
    static ErrorOr<void> bencode(BEncodingType, Stream&);
};
