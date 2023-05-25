/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Torrent.h"

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
    default:
        VERIFY_NOT_REACHED();
    }
}

Torrent::Torrent(NonnullOwnPtr<MetaInfo> meta_info, NonnullOwnPtr<Vector<NonnullRefPtr<LocalFile>>> local_files)
    : m_meta_info(move(meta_info))
    , m_piece_count(AK::ceil_div(m_meta_info->total_length(), m_meta_info->piece_length()))
    , m_local_bitfield(BitField(m_piece_count))
    , m_local_files(move(local_files))
    , m_display_name(m_meta_info->root_dir_name().value_or(m_meta_info->files()[0]->path()))
    , m_data_file_map(make<TorrentDataFileMap>(m_meta_info->piece_hashes(), m_meta_info->piece_length(), make<Vector<NonnullRefPtr<LocalFile>>>(*m_local_files)))
    , m_state(TorrentState::STOPPED)
{
}
}