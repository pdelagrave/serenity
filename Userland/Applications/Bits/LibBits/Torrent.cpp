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
    case TorrentState::SEEDING:
        return "Seeding"_string;
    default:
        VERIFY_NOT_REACHED();
    }
}

Torrent::Torrent(DeprecatedString display_name, NonnullOwnPtr<Vector<NonnullRefPtr<LocalFile>>> local_files, DeprecatedString data_path, InfoHash info_hash, PeerId local_peer_id, u64 total_length, u64 nominal_piece_length, u16 local_port, NonnullOwnPtr<TorrentDataFileMap> data_file_map)
    : display_name(display_name)
    , local_files(move(local_files))
    , data_path(move(data_path))
    , info_hash(info_hash)
    , local_peer_id(local_peer_id)
    , piece_count(ceil_div(total_length, nominal_piece_length))
    , nominal_piece_length(nominal_piece_length)
    , total_length(total_length)
    , local_port(local_port)
    , local_bitfield(BitField(piece_count))
    , data_file_map(move(data_file_map))
{
}

void Torrent::checking_in_background(bool skip, bool assume_valid, Function<void(BitField)> on_complete)
{
    background_checker = Threading::BackgroundAction<int>::construct(
        [this, skip, assume_valid](auto& task) -> ErrorOr<int> {
            piece_verified = 0;
            for (u64 i = 0; i < piece_count; i++) {
                piece_verified++;
                if (task.is_canceled())
                    return Error::from_errno(ECANCELED);
                bool is_present = skip ? assume_valid : TRY(data_file_map->check_piece(i, i == piece_count - 1));
                local_bitfield.set(i, is_present);
            }
            return 0;
        },
        [on_complete = move(on_complete)](auto result) -> ErrorOr<void> {
            on_complete(move(result));
            return {};
        },
        [this](auto error) {
            state = TorrentState::ERROR;
            warnln("Error checking torrent: {}", error);
        });
}

void Torrent::cancel_checking()
{
    if (background_checker) {
        background_checker->cancel();
        background_checker.clear();
    }
}

u64 Torrent::piece_length(u64 piece_index) const
{
    if (piece_index == piece_count - 1 && total_length % nominal_piece_length > 0)
        return total_length % nominal_piece_length;
    else
        return nominal_piece_length;
}

}