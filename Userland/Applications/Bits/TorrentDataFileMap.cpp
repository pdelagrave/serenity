/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "TorrentDataFileMap.h"
#include "BitField.h"
#include <AK/DeprecatedString.h>

namespace Bits {

TorrentDataFileMap::TorrentDataFileMap(ByteBuffer piece_hashes, i64 piece_length, i64 total_length, NonnullOwnPtr<Vector<DeprecatedString>> files)
    : m_piece_hashes(piece_hashes)
    , m_piece_length(piece_length)
    , m_total_length(total_length)
    , m_files(move(files))
{
}

bool TorrentDataFileMap::write_piece(u32, ByteBuffer const&)
{
    return true;
}

bool TorrentDataFileMap::verify_piece(i64)
{
    return true;
}

}