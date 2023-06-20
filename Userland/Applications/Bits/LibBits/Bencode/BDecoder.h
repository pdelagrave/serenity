/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "AK/DeprecatedString.h"
#include "AK/HashMap.h"
#include "AK/MemoryStream.h"
#include "AK/Stream.h"
#include "AK/Variant.h"
#include "AK/Vector.h"

namespace Bits {
struct List;
class Dict;

using BEncodingType = Variant<ByteBuffer, i64, List, Dict>;

struct List : public Vector<BEncodingType> {
    using Vector<BEncodingType>::Vector;
};

class Dict : public OrderedHashMap<DeprecatedString, BEncodingType> {
    using OrderedHashMap<DeprecatedString, BEncodingType>::OrderedHashMap;

public:
    template<typename T>
    T get(DeprecatedString key)
    {
        return OrderedHashMap<DeprecatedString, BEncodingType>::get(key).value().get<T>();
    }

    template<typename T>
    bool has(DeprecatedString key)
    {
        return OrderedHashMap<DeprecatedString, BEncodingType>::get(key).value().has<T>();
    }

    DeprecatedString get_string(DeprecatedString key)
    {
        return DeprecatedString::from_utf8(get<ByteBuffer>(key).bytes()).release_value();
    }
};

class BDecoder {
public:
    template<typename T>
    static ErrorOr<T> parse(Stream& stream)
    {
        return TRY(parse_bencoded(stream, nullptr)).get<T>();
    }

    template<typename T>
    static ErrorOr<T> parse(ReadonlyBytes& bytes)
    {
        auto stream = FixedMemoryStream(bytes);
        return TRY(parse<T>(stream));
    }

private:
    static ErrorOr<BEncodingType> parse_bencoded(Stream&, u8*);
    static ErrorOr<i64> parse_integer(Stream&);
    static ErrorOr<ByteBuffer> parse_byte_array(Stream&, u8);
    static ErrorOr<Dict> parse_dictionary(Stream&);
    static ErrorOr<List> parse_list(Stream&);
};
}