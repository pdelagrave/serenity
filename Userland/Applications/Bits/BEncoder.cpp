/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "BEncoder.h"

namespace Bits {
ErrorOr<void> BEncoder::bencode(BEncodingType object, Stream& stream)
{
    return object.visit(
        [&](ByteBuffer string) -> ErrorOr<void> {
            TRY(stream.write_until_depleted(DeprecatedString::number(string.size()).bytes()));
            TRY(stream.write_value<u8>(':'));
            TRY(stream.write_until_depleted(string.bytes()));
            return {};
        },
        [&](i64 number) -> ErrorOr<void> {
            TRY(stream.write_value<u8>('i'));
            TRY(stream.write_until_depleted(DeprecatedString::number(number).bytes()));
            TRY(stream.write_value<u8>('e'));
            return {};
        },
        [&](List) -> ErrorOr<void> {
            TODO();
            return {};
        },
        [&](Dict dict) -> ErrorOr<void> {
            TRY(stream.write_value<u8>('d'));
            for (auto const& item : dict) {
                TRY(bencode(TRY(ByteBuffer::copy(item.key.bytes())), stream));
                TRY(bencode(item.value, stream));
            }
            TRY(stream.write_value<u8>('e'));
            return {};
        });
}
}