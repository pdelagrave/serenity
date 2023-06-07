/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "BitField.h"

namespace Bits {
BitField::BitField(u64 size)
    : m_size(size)
{
    m_data.resize(AK::ceil_div(size, 8L));
    m_data.zero_fill();
}

BitField::BitField(ByteBuffer data, u64 size)
    : m_size(size)
{
    VERIFY(m_size > 0);
    VERIFY(data.size() > 0);
    VERIFY(data.size() == AK::ceil_div(size, 8L));
    m_data = move(data);
    for (u64 i = 0; i < m_size; i++) {
        if (get(i))
            m_ones++;
    }
}

bool BitField::get(u64 index) const
{
    if (index >= m_size) // useful for when the peer exists, and we haven't received its bitfield yet
        return false;
    return m_data[index / 8] & (1 << (7 - (index % 8)));
}

void BitField::set(u64 index, bool value)
{
    VERIFY(index < m_size);
    if (get(index) ^ value) {
        if (value) {
            m_ones++;
            m_data[index / 8] |= (1 << (7 - (index % 8)));
        } else {
            m_ones--;
            m_data[index / 8] &= ~(1 << (7 - (index % 8)));
        }
    }
}

ErrorOr<void> BitField::write_to_stream(Stream& stream) const
{
    TRY(stream.write_until_depleted(m_data.bytes()));
    return {};
}

ErrorOr<BitField> BitField::read_from_stream(Stream& stream) {
    // This works only when the bitfield is the last thing to be read from the stream
    // (which is the case for the BT bitfield message type)
    auto data = TRY(stream.read_until_eof());
    // this assumes the bitfield size is a multiple of 8 but the last bits could be padding
    return BitField(data, data.size() * 8);
}

}