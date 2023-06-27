/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "BitField.h"
#include "FixedSizeByteString.h"
#include "PieceHeap.h"
#include "TorrentDataFileMap.h"
#include <AK/HashMap.h>
#include <AK/NonnullRefPtr.h>
#include <AK/RefCounted.h>

namespace Bits {

struct PeerContext;
struct Peer;

struct TorrentContext : RefCounted<TorrentContext> {
    TorrentContext(InfoHash info_hash, PeerId local_peer_id, u64 total_length, u64 nominal_piece_length, u16 local_port, BitField local_bitfield, NonnullOwnPtr<TorrentDataFileMap> data_file_map);

    const InfoHash info_hash;
    const PeerId local_peer_id;
    const u64 piece_count;
    const u64 nominal_piece_length; // Is "nominal" used correctly here?
    const u64 total_length;
    const u16 local_port;

    BitField local_bitfield;
    OwnPtr<TorrentDataFileMap> data_file_map;

    PieceHeap piece_heap;
    HashMap<u64, RefPtr<PieceStatus>> missing_pieces;

    Vector<NonnullRefPtr<Peer>> peers;
    HashTable<NonnullRefPtr<PeerContext>> connected_peers;

    u64 download_speed { 0 };
    u64 upload_speed { 0 };

    u64 piece_length(u64 piece_index) const;
};

}
