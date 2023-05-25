/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "MetaInfo.h"
#include "Peer.h"
#include "TorrentDataFileMap.h"
#include <AK/NonnullOwnPtr.h>

namespace Bits {


enum class TorrentState {
    ERROR,
    CHECKING,
    STOPPED,
    STARTED
};
ErrorOr<String> state_to_string(TorrentState state);

class Torrent : public RefCounted<Torrent> {
public:
    Torrent(NonnullOwnPtr<MetaInfo>, NonnullOwnPtr<Vector<NonnullRefPtr<LocalFile>>>);
    MetaInfo& meta_info() { return *m_meta_info; }
    u64 piece_count() const { return m_piece_count; }
    TorrentState state() { return m_state; }
    void set_state(TorrentState state) { m_state = state; }
    NonnullOwnPtr<Vector<NonnullRefPtr<LocalFile>>> const& local_files() const { return m_local_files; }
    Vector<Peer>& peers() { return m_peers; }
    BitField& local_bitfield() { return m_local_bitfield; }
    DeprecatedString const& display_name() const { return m_display_name; }
    NonnullOwnPtr<TorrentDataFileMap> const& data_file_map() const { return m_data_file_map; }
    int progress() const { return m_local_bitfield.ones() * 100 / m_piece_count; }

private:
    NonnullOwnPtr<MetaInfo> m_meta_info;
    u64 m_piece_count;
    BitField m_local_bitfield;
    NonnullOwnPtr<Vector<NonnullRefPtr<LocalFile>>> m_local_files;
    DeprecatedString m_display_name;
    NonnullOwnPtr<TorrentDataFileMap> m_data_file_map;

    TorrentState m_state;
    Vector<Peer> m_peers;
};

}
