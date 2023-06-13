/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "PeerContext.h"
#include "TorrentContext.h"
#include <AK/Stack.h>
#include <LibThreading/Thread.h>

namespace Bits::Data {

class Comm : public Core::Object {
    C_OBJECT(Comm);

public:
    Comm();
    void activate_torrent(NonnullRefPtr<TorrentContext> torrent);
    void deactivate_torrent(ReadonlyBytes info_hash);
    void add_peers(ReadonlyBytes info_hash, Vector<Core::SocketAddress> peers);
    Optional<NonnullRefPtr<Data::TorrentContext>> get_torrent_context(ReadonlyBytes);
    Vector<NonnullRefPtr<Data::TorrentContext>> get_torrent_contexts();

    const u16 max_total_connections = 100;
    const u16 max_total_connections_per_torrent = 10;

protected:
    void timer_event(Core::TimerEvent&) override;

private:
    const u64 BlockLength = 16 * KiB;

    OwnPtr<Core::EventLoop> m_event_loop;
    RefPtr<Threading::Thread> m_thread;

    HashMap<ReadonlyBytes, NonnullRefPtr<TorrentContext>> m_torrent_contexts;
    timeval m_last_speed_measurement;

    // BT higher level logic
    ErrorOr<void> piece_downloaded(u64 index, ReadonlyBytes data, NonnullRefPtr<PeerContext> peer);
    ErrorOr<void> piece_or_peer_availability_updated(NonnullRefPtr<TorrentContext> torrent);
    ErrorOr<void> peer_has_piece(u64 piece_index, NonnullRefPtr<PeerContext> peer);
    void insert_piece_in_heap(NonnullRefPtr<TorrentContext> torrent, u64 piece_index);

    // Comm BT message handlers
    ErrorOr<void> handle_bitfield(NonnullOwnPtr<BitTorrent::BitFieldMessage>, NonnullRefPtr<PeerContext>);
    ErrorOr<void> handle_handshake(NonnullOwnPtr<BitTorrent::Handshake> handshake, NonnullRefPtr<PeerContext> pcontext);
    ErrorOr<void> handle_have(NonnullOwnPtr<BitTorrent::Have> have_message, NonnullRefPtr<PeerContext> pcontext);
    ErrorOr<void> handle_piece(NonnullOwnPtr<BitTorrent::Piece>, NonnullRefPtr<PeerContext> pcontext);
    ErrorOr<void> parse_input_message(SeekableStream& stream, NonnullRefPtr<PeerContext> peer);

    // Comm BT low level network functions
    ErrorOr<void> read_from_socket(NonnullRefPtr<PeerContext> pcontext);
    void send_message(NonnullOwnPtr<BitTorrent::Message> message, NonnullRefPtr<PeerContext> peer);
    void flush_output_buffer(NonnullRefPtr<PeerContext> peer);
    void connect_more_peers(NonnullRefPtr<TorrentContext>);
    ErrorOr<void> connect_to_peer(NonnullRefPtr<PeerContext> pcontext);
    void set_peer_errored(NonnullRefPtr<PeerContext> pcontext);
    u64 get_available_peers_count(NonnullRefPtr<TorrentContext> torrent) const;

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
