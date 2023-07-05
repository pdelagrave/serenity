/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Torrent.h"
#include "Peer.h"
#include "PeerContext.h"

namespace Bits {
ErrorOr<String> state_to_string(TorrentState state)
{
    switch (state) {
    case TorrentState::ERROR:
        return "Error"_string;
    case TorrentState::STOPPED:
        return "Stopped"_string;
    case TorrentState::STARTED:
        return "Started"_string;
    case TorrentState::CHECKING:
        return "Checking"_string;
    case TorrentState::CHECKING_CANCELLED:
        return "Checking Cancelled"_string;
    case TorrentState::CHECKING_FAILED:
        return "Checking Failed"_string;
    case TorrentState::SEEDING:
        return "Seeding"_string;
    default:
        VERIFY_NOT_REACHED();
    }
}

Torrent::Torrent(DeprecatedString display_name, Vector<NonnullRefPtr<LocalFile>> local_files, ByteBuffer piece_hashes, DeprecatedString data_path, InfoHash info_hash, PeerId local_peer_id, u64 total_length, u64 nominal_piece_length, u16 local_port)
    : display_name(display_name)
    , local_files(local_files)
    , piece_hashes(piece_hashes)
    , data_path(move(data_path))
    , info_hash(info_hash)
    , local_peer_id(local_peer_id)
    , tracker_session_key(get_random<u64>())
    , piece_count(ceil_div(total_length, nominal_piece_length))
    , nominal_piece_length(nominal_piece_length)
    , total_length(total_length)
    , local_port(local_port)
    , local_bitfield(BitField(piece_count))
    , data_file_map(make<TorrentDataFileMap>(piece_hashes, nominal_piece_length, local_files))
{
}

u64 Torrent::piece_length(u64 piece_index) const
{
    if (piece_index == piece_count - 1 && total_length % nominal_piece_length > 0)
        return total_length % nominal_piece_length;
    else
        return nominal_piece_length;
}

}