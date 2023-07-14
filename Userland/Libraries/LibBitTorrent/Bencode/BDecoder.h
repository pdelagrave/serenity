/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "BTypes.h"
#include <AK/MemoryStream.h>

namespace BitTorrent {

// The 'bencoding' section of https://www.bittorrent.org/beps/bep_0003.html
class BDecoder {

public:
    template<typename T>
    static ErrorOr<T> parse(ReadonlyBytes& bytes)
    {
        auto stream = FixedMemoryStream(bytes);
        return TRY(parse<T>(stream));
    }

    template<typename T>
    static ErrorOr<T> parse(Stream& stream)
    {
        return TRY(parse_bencoded(stream, nullptr)).get<T>();
    }

private:
    static ErrorOr<BEncodingType> parse_bencoded(Stream&, u8*);
    static ErrorOr<i64> parse_integer(Stream&);
    static ErrorOr<ByteBuffer> parse_byte_array(Stream&, u8);
    static ErrorOr<Dict> parse_dictionary(Stream&);
    static ErrorOr<List> parse_list(Stream&);
};

}