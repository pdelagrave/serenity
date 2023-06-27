/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "FixedSizeByteString.h"
#include "Net/Comm.h"
#include "Peer.h"
#include "PeerContext.h"
#include "Torrent.h"
#include "TorrentContext.h"
#include "TorrentView.h"
#include <AK/HashMap.h>
#include <LibCore/Object.h>
#include <LibProtocol/RequestClient.h>

namespace Bits {

class Engine : public Core::Object {
    C_OBJECT(Engine);

public:
    static ErrorOr<NonnullRefPtr<Engine>> try_create(bool skip_checking, bool assume_valid);
    ~Engine();

    void add_torrent(NonnullOwnPtr<MetaInfo>, DeprecatedString);
    HashMap<InfoHash, TorrentView> torrents();
    void start_torrent(InfoHash info_hash);
    void stop_torrent(InfoHash);
    void cancel_checking(InfoHash);

private:
    Engine(NonnullRefPtr<Protocol::RequestClient>, bool skip_checking, bool assume_valid);
    static ErrorOr<String> url_encode_bytes(u8 const* bytes, size_t length);
    static ErrorOr<void> create_file(DeprecatedString const& path);

    NonnullRefPtr<Protocol::RequestClient> m_protocol_client;
    HashTable<NonnullRefPtr<Protocol::Request>> m_active_requests;

    HashMap<InfoHash, NonnullRefPtr<Torrent>> m_torrents;
    ErrorOr<void> announce(Torrent& torrent, Function<void(Vector<Core::SocketAddress>)> on_complete);

    Comm m_comm;
    bool m_skip_checking;
    bool m_assume_valid;

    // #######################################################################################################
    const u16 max_total_connections = 100;
    const u16 max_connections_per_torrent = 10;
    // An upload slot is when a peer is connected to us, they are intested in us, we aren't interested in them.
    const u16 max_total_upload_slots = 50;
    const u16 max_upload_slots_per_torrent = 5;

    const u64 BlockLength = 16 * KiB;

    HashMap<InfoHash, NonnullRefPtr<TorrentContext>> m_torrent_contexts;
    HashMap<ConnectionId, NonnullRefPtr<Peer>> m_connecting_peers;
    HashMap<ConnectionId, NonnullRefPtr<PeerContext>> m_connected_peers;

    void connect_more_peers(NonnullRefPtr<TorrentContext>);
    u64 get_available_peers_count(NonnullRefPtr<TorrentContext> torrent) const;
    void peer_disconnected(ConnectionId connection_id);

    ErrorOr<void> piece_downloaded(u64 index, ReadonlyBytes data, NonnullRefPtr<PeerContext> peer);
    ErrorOr<void> piece_or_peer_availability_updated(NonnullRefPtr<TorrentContext> torrent);
    ErrorOr<void> peer_has_piece(u64 piece_index, NonnullRefPtr<PeerContext> peer);
    void insert_piece_in_heap(NonnullRefPtr<TorrentContext> torrent, u64 piece_index);

    ErrorOr<void> parse_input_message(ConnectionId connection_id, ReadonlyBytes message_bytes);
    ErrorOr<void> handle_bitfield(NonnullOwnPtr<BitFieldMessage>, NonnullRefPtr<PeerContext>);
    ErrorOr<void> handle_have(NonnullOwnPtr<HaveMessage> have_message, NonnullRefPtr<PeerContext> pcontext);
    ErrorOr<void> handle_interested(NonnullRefPtr<PeerContext> peer);
    ErrorOr<void> handle_piece(NonnullOwnPtr<PieceMessage>, NonnullRefPtr<PeerContext> pcontext);
    ErrorOr<void> handle_request(NonnullOwnPtr<RequestMessage>, NonnullRefPtr<PeerContext> pcontext);
};

}