/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "TorrentDataFileMap.h"
#include "BitField.h"

namespace Bits {

TorrentDataFileMap::TorrentDataFileMap(ByteBuffer piece_hashes, i64 piece_length, NonnullOwnPtr<Vector<NonnullRefPtr<LocalFile>>> files)
    : m_piece_hashes(piece_hashes)
    , m_piece_length(piece_length)
    , m_files(move(files))
{
    for (const auto & item : *m_files) {
        m_total_length += item->meta_info_file()->length();
    }
}

bool TorrentDataFileMap::write_piece(u32, ByteBuffer const&)
{
    return true;
}

bool TorrentDataFileMap::verify_piece(i64 index)
{
    auto piece_hash = m_piece_hashes.slice(index * 20, 20);

    return true;
}

}