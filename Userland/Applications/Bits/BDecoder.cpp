/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "BDecoder.h"

// Reference: The 'bencoding' section of https://www.bittorrent.org/beps/bep_0003.html

namespace Bits {
static constexpr bool is_digit(char c)
{
    return c <= '9' && c >= '0';
}

ErrorOr<BEncodingType> BDecoder::parse_bencoded(Stream& stream)
{
    return parse_bencoded(stream, nullptr);
}

ErrorOr<BEncodingType> BDecoder::parse_bencoded(Stream& stream, u8* byte_already_read)
{
    const u8 next_byte = byte_already_read != nullptr ? *byte_already_read : TRY(stream.read_value<u8>());
    if (next_byte == 'i') {
        return BEncodingType(TRY(parse_integer(stream)));
    } else if (next_byte == 'l') {
        return parse_list(stream);
    } else if (next_byte == 'd') {
        return parse_dictionary(stream);
    } else if (is_digit(next_byte)) {
        return parse_byte_array(stream, next_byte);
    }

    return Error::from_string_literal("Can't parse type");
}

ErrorOr<i64> BDecoder::parse_integer(Stream& stream)
{
    auto integer_str = StringBuilder();
    u8 digit;
    while ((digit = TRY(stream.read_value<u8>())) != 'e') {
        if (is_digit(digit) || digit == '-') {
            if (digit == '-' && integer_str.length() != 0)
                return Error::from_string_literal("Invalid integer: When used, minus sign must be the first character.");
            else if (integer_str.string_view() == "0")
                return Error::from_string_literal("Invalid integer: Leading 0s not allowed.");
            else if (integer_str.string_view() == '-' && digit == '0')
                return Error::from_string_literal("Invalid integer: Leading 0s and -0 not allowed.");

            integer_str.append(digit);
        } else {
            return Error::from_string_literal("Invalid integer, valid characters are 0-9 and -");
        }
    }
    // The BEP says there's no limit to integer size but let's keep it to i64 here.
    auto ret = AK::StringUtils::convert_to_int<i64>(integer_str.string_view());
    if (ret.has_value())
        return ret.value();
    else
        return Error::from_string_literal("Invalid integer, likely out of bound");
}

ErrorOr<ByteBuffer> BDecoder::parse_byte_array(Stream& stream, u8 first_byte)
{
    auto array_size_str = StringBuilder();
    u8 digit = first_byte;
    do {
        // TODO: limit on the size of the array
        if (digit >= '0' && digit <= '9') {
            array_size_str.append(digit);
        } else {
            return Error::from_string_literal("Invalid byte array size");
        }
    } while ((digit = TRY(stream.read_value<u8>())) != ':');
    auto array_size = AK::StringUtils::convert_to_uint<u64>(array_size_str.string_view(), AK::TrimWhitespace::No).value();
    auto buffer = TRY(ByteBuffer::create_uninitialized(array_size));
    TRY(stream.read_until_filled(buffer));

    return buffer;
}

ErrorOr<bencoded_dict> BDecoder::parse_dictionary(Stream& stream)
{
    auto dict = bencoded_dict();
    auto previous_key = m_empty_string;
    u8 next_byte;
    while ((next_byte = TRY(stream.read_value<u8>())) != 'e') {
        auto buffer = TRY(parse_byte_array(stream, next_byte)); // key is always expected to be a byte array (string).
        auto key = TRY(String::from_utf8(StringView(buffer.bytes())));
        if (key < previous_key)
            warnln("Invalid dictionary: entries key must be sorted"); // but many trackers don't sort them.
        BEncodingType const& value = TRY(parse_bencoded(stream));
        dict.set(key, value);
        previous_key = key;
    }
    return dict;
}

ErrorOr<bencoded_list> BDecoder::parse_list(Stream& stream)
{
    // TODO: check if items are all the same type
    auto list = bencoded_list();
    u8 next_byte;
    while ((next_byte = TRY(stream.read_value<u8>())) != 'e') {
        list.append(TRY(parse_bencoded(stream, &next_byte)));
    }
    return list;
}

}