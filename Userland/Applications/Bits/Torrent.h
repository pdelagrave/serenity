/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "Applications/Bits/BK/PieceHeap.h"
#include "MetaInfo.h"
#include "Peer.h"
#include "TorrentDataFileMap.h"
#include <AK/NonnullOwnPtr.h>
#include <LibThreading/BackgroundAction.h>

namespace Bits {

class Peer;

enum class TorrentState {
    ERROR,
    CHECKING,
    STOPPED,
    STARTED,
    SEEDING
};
ErrorOr<String> state_to_string(TorrentState state);

class Torrent : public RefCounted<Torrent> {
    using MissingPieceMap = AK::HashMap<u64, RefPtr<PieceStatus>>;
public:
    Torrent(NonnullOwnPtr<MetaInfo>, NonnullOwnPtr<Vector<NonnullRefPtr<LocalFile>>>);
    ~Torrent();
    MetaInfo& meta_info() { return *m_meta_info; }
    u64 piece_count() const { return m_piece_count; }
    ReadonlyBytes local_peer_id() const { return m_local_peer_id.bytes(); }
    TorrentState state() { return m_state; }
    void set_state(TorrentState state) { m_state = state; }
    NonnullOwnPtr<Vector<NonnullRefPtr<LocalFile>>> const& local_files() const { return m_local_files; }
    Vector<NonnullRefPtr<Peer>>& peers() { return m_peers; }
    BitField& local_bitfield() { return m_local_bitfield; }
    u16 local_port() const { return m_local_port; }
    DeprecatedString const& display_name() const { return m_display_name; }
    NonnullOwnPtr<TorrentDataFileMap> const& data_file_map() const { return m_data_file_map; }
    float check_progress() const { return (float) m_piece_verified * 100 / (float) m_piece_count; }

    void checking_in_background(bool skip, bool assume_valid, Function<void()> on_complete);
    void cancel_checking();

    BK::PieceHeap& piece_heap() { return m_piece_heap; }
    MissingPieceMap& missing_pieces() { return m_missing_pieces; }

    u64 piece_length(u64 piece_index) const;

private:
    NonnullOwnPtr<MetaInfo> m_meta_info;
    u64 m_piece_count;
    BitField m_local_bitfield;
    ByteBuffer m_local_peer_id;
    u16 m_local_port { 27007 };
    NonnullOwnPtr<Vector<NonnullRefPtr<LocalFile>>> m_local_files;
    DeprecatedString m_display_name;
    NonnullOwnPtr<TorrentDataFileMap> m_data_file_map;
    RefPtr<Threading::BackgroundAction<int>> m_background_checker;
    u64 m_piece_verified = 0;

    TorrentState m_state;
    Vector<NonnullRefPtr<Peer>> m_peers;

    BK::PieceHeap m_piece_heap;
    MissingPieceMap m_missing_pieces;
};

}
