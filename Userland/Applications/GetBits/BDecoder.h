/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/HashMap.h>
#include <AK/Stream.h>
#include <AK/String.h>
#include <AK/Variant.h>
#include <AK/Vector.h>

struct bencoded_list;
struct bencoded_dict;

using BEncodingType = Variant<ByteBuffer, i64, bencoded_list, bencoded_dict>;

struct bencoded_list : public AK::Vector<BEncodingType> {
    using AK::Vector<BEncodingType>::Vector;
};

struct bencoded_dict : public AK::OrderedHashMap<String, BEncodingType> {
    using AK::OrderedHashMap<String, BEncodingType>::OrderedHashMap;
};

class BDecoder {
public:
    static ErrorOr<BEncodingType> parse_bencoded(AK::SeekableStream&);

private:
    static constexpr String m_empty_string = String();
    static ErrorOr<u8> peek_next_byte(AK::SeekableStream&);
    static ErrorOr<i64> parse_integer(AK::SeekableStream&);
    static ErrorOr<ByteBuffer> parse_byte_array(AK::SeekableStream&);
    static ErrorOr<bencoded_dict> parse_dictionary(AK::SeekableStream&);
    static ErrorOr<bencoded_list> parse_list(AK::SeekableStream&);
};
