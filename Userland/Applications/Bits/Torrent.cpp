/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Torrent.h"
#include <AK/Random.h>

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

Torrent::Torrent(NonnullOwnPtr<MetaInfo> meta_info, NonnullOwnPtr<Vector<NonnullRefPtr<LocalFile>>> local_files, DeprecatedString data_path)
    : m_meta_info(move(meta_info))
    , m_piece_count(AK::ceil_div(m_meta_info->total_length(), m_meta_info->piece_length()))
    , m_local_files(move(local_files))
    , m_display_name(m_meta_info->root_dir_name().value_or(m_meta_info->files()[0]->path()))
    , m_data_file_map(make<TorrentDataFileMap>(m_meta_info->piece_hashes(), m_meta_info->piece_length(), make<Vector<NonnullRefPtr<LocalFile>>>(*m_local_files)))
    , m_state(TorrentState::STOPPED)
    , m_data_path(move(data_path))
{
    m_local_peer_id.resize(20);
    fill_with_random({ m_local_peer_id.data(), m_local_peer_id.size() });
}

Torrent::~Torrent()
{
    dbgln("Torrent::~Torrent()");
    if (m_background_checker)
        m_background_checker->cancel();
}

void Torrent::checking_in_background(bool skip, bool assume_valid, Function<void(BitField)> on_complete)
{
    m_background_checker = Threading::BackgroundAction<BitField>::construct(
        [this, skip, assume_valid](auto& task) -> ErrorOr<BitField> {
            auto local_bitfield = BitField(m_piece_count);
            m_piece_verified = 0;
            for (u64 i = 0; i < piece_count(); i++) {
                m_piece_verified++;
                if (task.is_canceled())
                    return Error::from_errno(ECANCELED);
                bool is_present = skip ? assume_valid : TRY(data_file_map()->check_piece(i, i == piece_count() - 1));
                local_bitfield.set(i, is_present);
            }
            return local_bitfield;
        },
        [on_complete = move(on_complete)](auto result) -> ErrorOr<void> {
            on_complete(move(result));
            return {};
        },
        [this](auto error) {
            m_state = TorrentState::ERROR;
            warnln("Error checking torrent: {}", error);
        });
}

void Torrent::cancel_checking()
{
    if (m_background_checker) {
        m_background_checker->cancel();
        m_background_checker.clear();
    }
}

u64 Torrent::piece_length(u64 piece_index) const
{
    VERIFY(piece_index < piece_count());
    if (piece_index == piece_count() - 1)
        return m_meta_info->total_length() % m_meta_info->piece_length();
    else
        return m_meta_info->piece_length();
}

}