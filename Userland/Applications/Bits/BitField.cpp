/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "BitField.h"

namespace Bits {
BitField::BitField(u64 size)
    : m_size(size)
    , m_data(make<ByteBuffer>())
{
    m_data->resize(AK::ceil_div(size, 8L));
    m_data->zero_fill();
    m_ones = 0;
}

BitField::BitField(NonnullOwnPtr<ByteBuffer> data)
    : m_size(data->size() * 8)
    , m_data(move(data))
{
    VERIFY(m_size > 0);
    VERIFY(m_data->size() > 0);
}

bool BitField::get(u64 index) const
{
    VERIFY(index < m_size);
    return (*m_data)[index / 8] & (1 << (7 - (index % 8)));
}

void BitField::set(u64 index, bool value)
{
    VERIFY(index < m_size);
    if (get(index) ^ value) {
        if (value) {
            m_ones++;
            (*m_data)[index / 8] |= (1 << (7 - (index % 8)));
        } else {
            m_ones--;
            (*m_data)[index / 8] &= ~(1 << (7 - (index % 8)));
        }
    }
}

}