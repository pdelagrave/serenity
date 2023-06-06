/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/Types.h>

namespace Bits {
class BitField {
public:
    BitField(u64 size);
    BitField(ByteBuffer data, u64 size);

    bool get(u64 index) const;
    void set(u64 index, bool value);
    u64 ones() const { return m_ones; }
    u64 zeroes() const { return m_size - m_ones; }

    u64 size() const { return m_size; }
    u64 data_size() const { return m_data.size(); }

    ReadonlyBytes bytes() const { return m_data.bytes(); }

private:
    u64 m_size;
    ByteBuffer m_data {};
    u64 m_ones = 0;
};
}
