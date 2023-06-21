/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/Stream.h>
#include <AK/Types.h>

namespace Bits {
class BitField {
public:
    BitField(u64 size);
    BitField(ReadonlyBytes data, u64 size);
    //BitField(BitField const& other);

    bool get(u64 index) const;
    void set(u64 index, bool value);
    u64 ones() const { return m_ones; }
    u64 zeroes() const { return m_size - m_ones; }
    float progress() const { return (float)m_ones * 100 / (float)m_size; }

    u64 size() const { return m_size; }
    u64 data_size() const { return m_data.size(); }

    ReadonlyBytes bytes() const { return m_data.bytes(); }
    ErrorOr<void> write_to_stream(Stream& stream) const;
    static ErrorOr<BitField> read_from_stream(Stream& stream, u64 size);

private:
    u64 m_size;
    ByteBuffer m_data {};
    u64 m_ones = 0;
};
}

template<>
struct AK::Formatter<Bits::BitField> : AK::Formatter<FormatString> {
    ErrorOr<void> format(FormatBuilder& builder, Bits::BitField const& value)
    {
        return Formatter<FormatString>::format(builder, "{}/{} ({:.2}%), storage: {}b, "sv, value.ones(), value.size(), value.progress(), value.data_size());
    }
};