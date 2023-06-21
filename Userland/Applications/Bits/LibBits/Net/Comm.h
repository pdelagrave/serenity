/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../TorrentView.h"
#include "LibThreading/Mutex.h"
#include "PeerContext.h"
#include "TorrentContext.h"
#include <AK/Stack.h>
#include <LibCore/TCPServer.h>
#include <LibThreading/Thread.h>

namespace Bits {

class Comm : public Core::Object {
    C_OBJECT(Comm);

public:
    Comm();
    void activate_torrent(NonnullRefPtr<TorrentContext> torrent);
    void deactivate_torrent(InfoHash info_hash);
    void add_peers(InfoHash info_hash, Vector<Core::SocketAddress> peers);
    HashMap<InfoHash, TorrentView> state_snapshot();

    const u16 max_total_connections = 100;
    const u16 max_connections_per_torrent = 10;
    // An upload slot is when a peer is connected to us, they are intested in us, we aren't interested in them.
    const u16 max_total_upload_slots = 50;
    const u16 max_upload_slots_per_torrent = 5;

protected:
    void timer_event(Core::TimerEvent&) override;

private:
    const u64 BlockLength = 16 * KiB;

    OwnPtr<Core::EventLoop> m_event_loop;
    RefPtr<Threading::Thread> m_thread;
    NonnullRefPtr<Core::TCPServer> m_server;

    HashMap<InfoHash, NonnullRefPtr<TorrentContext>> m_torrent_contexts;
    timeval m_last_speed_measurement;
    HashMap<InfoHash, TorrentView> m_state_snapshot;
    Threading::Mutex m_state_snapshot_lock;

    // BT higher level logic
    ErrorOr<void> piece_downloaded(u64 index, ReadonlyBytes data, NonnullRefPtr<PeerContext> peer);
    ErrorOr<void> piece_or_peer_availability_updated(NonnullRefPtr<TorrentContext> torrent);
    ErrorOr<void> peer_has_piece(u64 piece_index, NonnullRefPtr<PeerContext> peer);
    void insert_piece_in_heap(NonnullRefPtr<TorrentContext> torrent, u64 piece_index);

    // Comm BT message handlers
    ErrorOr<void> parse_input_message(SeekableStream& stream, NonnullRefPtr<PeerContext> peer);
    ErrorOr<void> handle_bitfield(NonnullOwnPtr<BitFieldMessage>, NonnullRefPtr<PeerContext>);
    ErrorOr<void> handle_have(NonnullOwnPtr<HaveMessage> have_message, NonnullRefPtr<PeerContext> pcontext);
    ErrorOr<void> handle_interested(NonnullRefPtr<PeerContext> peer);
    ErrorOr<void> handle_piece(NonnullOwnPtr<PieceMessage>, NonnullRefPtr<PeerContext> pcontext);
    ErrorOr<void> handle_request(NonnullOwnPtr<RequestMessage>, NonnullRefPtr<PeerContext> pcontext);

    // Comm BT low level network functions
    HashMap<NonnullRefPtr<PeerConnection>, NonnullRefPtr<PeerContext>> m_connecting_peers;
    HashTable<NonnullRefPtr<PeerConnection>> m_accepted_connections;

    ErrorOr<void> read_from_socket(NonnullRefPtr<PeerConnection> connection);
    void send_message(NonnullOwnPtr<Message> message, NonnullRefPtr<PeerContext> peer);
    ErrorOr<void> flush_output_buffer(NonnullRefPtr<PeerConnection> connection);
    void connect_more_peers(NonnullRefPtr<TorrentContext>);
    ErrorOr<void> connect_to_peer(NonnullRefPtr<PeerContext> pcontext);
    ErrorOr<void> send_handshake(InfoHash info_hash, PeerId local_peer_id, NonnullRefPtr<PeerConnection> connection);
    void set_peer_errored(NonnullRefPtr<PeerContext> peer, bool should_connect_more_peers = true);
    void close_connection(NonnullRefPtr<PeerConnection> connection);
    u64 get_available_peers_count(NonnullRefPtr<TorrentContext> torrent) const;
    ErrorOr<void> on_ready_to_accept();

    // dbgln with Context
    Vector<NonnullRefPtr<PeerContext>> m_peer_context_stack;

    template<typename... Parameters>
    void dbglnc(CheckedFormatString<Parameters...>&& fmtstr, Parameters const&... parameters)
    {
        StringBuilder context_prefix;
        for (auto const& context : m_peer_context_stack) {
            context_prefix.appendff("[{:21}]", context->address);
        }
        if (m_peer_context_stack.size() > 0)
            context_prefix.appendff(" ");

        dbgln("{}{}", context_prefix.to_deprecated_string(), String::formatted(move(fmtstr), parameters...).value());
    }
};

}
