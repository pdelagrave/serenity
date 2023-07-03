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
#include <AK/NonnullOwnPtr.h>
#include <AK/NonnullRefPtr.h>
#include <AK/RefCounted.h>
#include <AK/URL.h>
#include <LibThreading/BackgroundAction.h>

namespace Bits {

struct PeerContext;
struct Peer;

enum class TorrentState {
    ERROR,
    CHECKING,
    STOPPED,
    STARTED,
    SEEDING
};

ErrorOr<String> state_to_string(TorrentState state);

struct Torrent : public RefCounted<Torrent> {
    Torrent(DeprecatedString display_name, NonnullOwnPtr<Vector<NonnullRefPtr<LocalFile>>>, DeprecatedString data_path, InfoHash info_hash, PeerId local_peer_id, u64 total_length, u64 nominal_piece_length, u16 local_port, NonnullOwnPtr<TorrentDataFileMap> data_file_map);

    const DeprecatedString display_name;
    NonnullOwnPtr<Vector<NonnullRefPtr<LocalFile>>> const local_files;
    const DeprecatedString data_path;
    const InfoHash info_hash;
    const PeerId local_peer_id;
    const u64 piece_count;
    const u64 nominal_piece_length; // Is "nominal" used correctly here?
    const u64 total_length;
    const u16 local_port;
    BitField local_bitfield;
    NonnullOwnPtr<TorrentDataFileMap> const data_file_map;

    URL announce_url;
    TorrentState state = TorrentState::STOPPED;

    // Active torrent members
    PieceHeap piece_heap;
    HashMap<u64, RefPtr<PieceStatus>> missing_pieces;
    Vector<NonnullRefPtr<Peer>> peers;
    HashTable<NonnullRefPtr<PeerContext>> connected_peers;
    u64 download_speed { 0 };
    u64 upload_speed { 0 };

    // Checking members
    RefPtr<Threading::BackgroundAction<int>> background_checker;
    u64 piece_verified = 0;
    void checking_in_background(bool skip, bool assume_valid, Function<void(BitField)> on_complete);
    void cancel_checking();

    u64 piece_length(u64 piece_index) const;
};

}
