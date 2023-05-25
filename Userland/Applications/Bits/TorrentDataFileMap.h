/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once
#include "BitField.h"
#include "Files.h"
#include <AK/ByteBuffer.h>
#include <AK/Forward.h>
#include <AK/Vector.h>

namespace Bits {


class TorrentDataFileMap {
public:
    TorrentDataFileMap(ByteBuffer piece_hashes, i64 piece_length, NonnullOwnPtr<Vector<NonnullRefPtr<LocalFile>>> files);
    bool write_piece(u32 index, ByteBuffer const& data);
    bool verify_piece(i64);

private:
    ByteBuffer m_piece_hashes;
    i64 m_piece_length;
    i64 m_total_length;

    NonnullOwnPtr<Vector<NonnullRefPtr<LocalFile>>> m_files;
};

}
