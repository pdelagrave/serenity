/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "BDecoder.h"
#include <AK/String.h>
#include <AK/StringUtils.h>

ErrorOr<u8> BDecoder::peek_next_byte(SeekableStream& stream)
{
    u8 next_byte = stream.read_value<u8>().release_value();
    TRY(stream.seek(-sizeof(u8), SeekMode::FromCurrentPosition));
    return next_byte;
}

ErrorOr<BEncodingType> BDecoder::parse_bencoded(SeekableStream& stream)
{
    u8 next_byte = TRY(peek_next_byte(stream));
    if (next_byte == 'i') {
        return BEncodingType(TRY(parse_integer(stream)));
    } else if (next_byte == 'l') {
        return parse_list(stream);
    } else if (next_byte == 'd') {
        return parse_dictionary(stream);
    } else if (next_byte >= '0' && next_byte <= '9') {
        return parse_byte_array(stream);
    }

    return Error::from_string_literal("Can't parse type");
}

ErrorOr<i64> BDecoder::parse_integer(SeekableStream& stream)
{
    VERIFY(TRY(stream.read_value<u8>()) == 'i');
    // TODO check for overflow
    auto integer_str = StringBuilder();
    u8 digit;
    while ((digit = TRY(stream.read_value<u8>())) != 'e') {
        if (digit >= '0' && digit <= '9') {
            integer_str.append(digit);
        } else {
            return Error::from_string_literal("Invalid integer");
        }
    }
    return AK::StringUtils::convert_to_int<i64>(integer_str.string_view(), AK::TrimWhitespace::No).value();
}

ErrorOr<ByteBuffer> BDecoder::parse_byte_array(SeekableStream& stream)
{
    auto array_size_str = StringBuilder();
    u8 digit;
    while ((digit = TRY(stream.read_value<u8>())) != ':') {
        // TODO: limit on the size of the array
        if (digit >= '0' && digit <= '9') {
            array_size_str.append(digit);
        } else {
            return Error::from_string_literal("Invalid byte array size");
        }
    }
    auto array_size = AK::StringUtils::convert_to_uint<u64>(array_size_str.string_view(), AK::TrimWhitespace::No).value();
    auto buffer = TRY(ByteBuffer::create_uninitialized(array_size));
    TRY(stream.read_until_filled(buffer));

    return buffer;
}

ErrorOr<bencoded_dict> BDecoder::parse_dictionary(SeekableStream& stream)
{
    VERIFY(TRY(stream.read_value<u8>()) == 'd');
    auto dict = bencoded_dict();
    while (TRY(peek_next_byte(stream)) != 'e') {
        auto buffer = TRY(parse_byte_array(stream));
        auto key = TRY(String::from_utf8(StringView(buffer.bytes())));
        BEncodingType const& value = TRY(parse_bencoded(stream));
        dict.set(key, value);
    }
    VERIFY(TRY(stream.read_value<u8>()) == 'e');
    return dict;
}

ErrorOr<bencoded_list> BDecoder::parse_list(SeekableStream& stream)
{
    // TODO: check item are all the same type
    VERIFY(TRY(stream.read_value<u8>()) == 'l');
    auto list = bencoded_list();
    while (TRY(peek_next_byte(stream)) != 'e') {
        list.append(TRY(parse_bencoded(stream)));
    }
    VERIFY(TRY(stream.read_value<u8>()) == 'e');
    return list;
}