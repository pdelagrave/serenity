/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "Announcer.h"
#include "Checker.h"
#include "FixedSizeByteString.h"
#include "MetaInfo.h"
#include "Net/Comm.h"
#include "Peer.h"
#include "PeerSession.h"
#include "Torrent.h"
#include "TorrentView.h"
#include <AK/HashMap.h>
#include <LibCore/Object.h>

namespace Bits {

class Engine : public Core::Object {
    C_OBJECT(Engine);

public:
    u64 const max_total_connections = 100;
    u64 const max_connections_per_torrent = 10;
    // An upload slot is when a peer is connected to us, they are intested in us, we aren't interested in them.
    u64 const max_total_upload_slots = 50;
    u64 const max_upload_slots_per_torrent = 5;

    void add_torrent(NonnullOwnPtr<MetaInfo>, DeprecatedString);
    void start_torrent(InfoHash info_hash);
    void stop_torrent(InfoHash);
    void cancel_checking(InfoHash);
    void register_views_update_callback(int interval_ms, Function<void(NonnullOwnPtr<HashMap<InfoHash, TorrentView>>)> callback);

private:
    Engine();

    u64 const BLOCK_LENGTH = 16 * KiB;

    OwnPtr<Core::EventLoop> m_event_loop;
    RefPtr<Threading::Thread> m_thread;

    Checker m_checker;
    Comm m_comm;

    HashMap<InfoHash, NonnullRefPtr<Announcer>> m_announcers;
    HashMap<InfoHash, NonnullRefPtr<Torrent>> m_torrents;

    HashMap<ConnectionId, NonnullRefPtr<Peer>> m_connecting_peers;
    HashMap<ConnectionId, NonnullRefPtr<PeerSession>> m_all_sessions;

    NonnullOwnPtr<HashMap<ConnectionId, ConnectionStats>> m_connection_stats { make<HashMap<ConnectionId, ConnectionStats>>() };
    CheckerStats m_checker_stats;

    NonnullOwnPtr<HashMap<InfoHash, TorrentView>> torrents_views();
    void check_torrent(NonnullRefPtr<Torrent> torrent, Function<void()> on_success);

    void connect_more_peers(NonnullRefPtr<Torrent>);
    ErrorOr<void> piece_downloaded(u64 index, ReadonlyBytes data, NonnullRefPtr<PeerSession> peer);
    ErrorOr<void> piece_or_peer_availability_updated(NonnullRefPtr<Torrent> torrent);
    ErrorOr<void> peer_has_piece(u64 piece_index, NonnullRefPtr<PeerSession> peer);
    void insert_piece_in_heap(NonnullRefPtr<Torrent> torrent, u64 piece_index);

    ErrorOr<void> parse_input_message(ConnectionId connection_id, ReadonlyBytes message_bytes);
    ErrorOr<void> handle_bitfield(NonnullOwnPtr<BitFieldMessage>, NonnullRefPtr<PeerSession>);
    ErrorOr<void> handle_have(NonnullOwnPtr<HaveMessage> have_message, NonnullRefPtr<PeerSession> session);
    ErrorOr<void> handle_interested(NonnullRefPtr<PeerSession> session);
    ErrorOr<void> handle_piece(NonnullOwnPtr<PieceMessage>, NonnullRefPtr<PeerSession> session);
    ErrorOr<void> handle_request(NonnullOwnPtr<RequestMessage>, NonnullRefPtr<PeerSession> session);
};

}