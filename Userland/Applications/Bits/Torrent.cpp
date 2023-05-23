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

Torrent::Torrent(NonnullOwnPtr<MetaInfo> meta_info, DeprecatedString data_path)
    : m_meta_info(move(meta_info))
    , m_data_path(data_path)
    , m_state(TorrentState::STOPPED)
{
}
}