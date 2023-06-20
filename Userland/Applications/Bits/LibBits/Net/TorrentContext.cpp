/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "PeerContext.h"
#include "TorrentContext.h"

namespace Bits {

TorrentContext::TorrentContext(ReadonlyBytes info_hash, ReadonlyBytes local_peer_id, u64 total_length, u64 nominal_piece_length, u16 local_port, BitField local_bitfield, NonnullOwnPtr<TorrentDataFileMap> data_file_map)
    : info_hash(info_hash)
    , local_peer_id(local_peer_id)
    , piece_count(ceil_div(total_length, nominal_piece_length))
    , nominal_piece_length(nominal_piece_length)
    , total_length(total_length)
    , local_port(local_port)
    , local_bitfield(move(local_bitfield))
    , data_file_map(move(data_file_map))
{
}

u64 TorrentContext::piece_length(u64 piece_index) const
{
    if (piece_index == piece_count - 1 && total_length % nominal_piece_length > 0)
        return total_length % nominal_piece_length;
    else
        return nominal_piece_length;
}

}