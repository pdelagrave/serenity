/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "Command.h"
#include "PeerContext.h"
#include "TorrentContext.h"
#include <AK/Stack.h>
#include <LibThreading/Mutex.h>

namespace Bits::Data {

class Comm : public Core::Object {
    C_OBJECT(Comm);

public:
    Comm();
    ErrorOr<void> activate_torrent(NonnullOwnPtr<ActivateTorrentCommand> command);
    ErrorOr<void> add_peers(AddPeersCommand command);
    Optional<NonnullRefPtr<Data::TorrentContext>> get_torrent_context(ReadonlyBytes);
    Vector<NonnullRefPtr<Data::TorrentContext>> get_torrent_contexts();

    // TODO: remove the peercontext->active system, because it's not respecting the connection limits.
    const u16 max_total_connections = 100;
    const u16 max_total_connections_per_torrent = 10;

protected:
    void custom_event(Core::CustomEvent& event) override;
    void timer_event(Core::TimerEvent&) override;

private:
    // TODO find the best request block length value
    // https://wiki.theory.org/BitTorrentSpecification#request:_.3Clen.3D0013.3E.3Cid.3D6.3E.3Cindex.3E.3Cbegin.3E.3Clength.3E
    const u64 BlockLength = 16 * KiB;

    OwnPtr<Core::EventLoop> m_event_loop;
    RefPtr<Threading::Thread> m_thread;

    HashMap<ReadonlyBytes, NonnullRefPtr<TorrentContext>> m_torrent_contexts;
    timeval m_last_speed_measurement;

    // Bits Engine/Comm internal commands
    ErrorOr<void> post_command(NonnullOwnPtr<Command> command);
    ErrorOr<void> handle_command_activate_torrent(ActivateTorrentCommand const& command);
    ErrorOr<void> handle_command_add_peers(AddPeersCommand const& command);
    ErrorOr<void> handle_command_piece_downloaded(PieceDownloadedCommand const& command);

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

    // BT higher level logic
    ErrorOr<void> piece_or_peer_availability_updated(NonnullRefPtr<TorrentContext> torrent);
    ErrorOr<bool> update_piece_availability(u64 piece_index, NonnullRefPtr<PeerContext> pcontext);
    void insert_piece_in_heap(NonnullRefPtr<TorrentContext> torrent, u64 piece_index);

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
