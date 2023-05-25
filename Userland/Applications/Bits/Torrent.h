/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "MetaInfo.h"
#include "Peer.h"
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
    Torrent(NonnullOwnPtr<MetaInfo>, DeprecatedString);
    MetaInfo& meta_info() { return *m_meta_info; }
    TorrentState state() { return m_state; }
    void set_state(TorrentState state) { m_state = state; }
    DeprecatedString data_path() { return m_data_path; }
    Vector<Peer>& peers() { return m_peers; }
    ByteBuffer const& local_bitfield() { return m_local_bitfield; }
    DeprecatedString const& display_name() const { return m_display_name; }

private:
    const NonnullOwnPtr<MetaInfo> m_meta_info;
    const u64 m_piece_count;
    const ByteBuffer m_local_bitfield;
    const DeprecatedString m_data_path;
    const DeprecatedString m_display_name;

    TorrentState m_state;
    Vector<Peer> m_peers;
};

}
