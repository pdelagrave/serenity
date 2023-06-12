/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "Command.h"
#include "PeerContext.h"
#include "TorrentContext.h"
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

    HashMap<ReadonlyBytes, NonnullRefPtr<TorrentContext>> m_tcontexts;
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

    // Comm BT low level network functions
    ErrorOr<void> read_from_socket(NonnullRefPtr<PeerContext> pcontext);
    ErrorOr<void> send_message(NonnullOwnPtr<BitTorrent::Message> message, NonnullRefPtr<PeerContext> context, RefPtr<PeerContext> parent_context = {});
    void flush_output_buffer(NonnullRefPtr<PeerContext> context, RefPtr<PeerContext> parent_context = {});
    void connect_more_peers(NonnullRefPtr<TorrentContext>, RefPtr<PeerContext> forlogging = {});
    ErrorOr<void> connect_to_peer(NonnullRefPtr<PeerContext> pcontext);
    void set_peer_errored(NonnullRefPtr<PeerContext> pcontext);

    // BT higher level logic
    ErrorOr<void> piece_or_peer_availability_updated(RefPtr<PeerContext> pcontext);
    ErrorOr<bool> update_piece_availability(u64 piece_index, NonnullRefPtr<PeerContext> pcontext);
    void insert_piece_in_heap(NonnullRefPtr<TorrentContext> torrent, u64 piece_index);

    // dbgln with Context
    template<typename... Parameters>
    void dbglnc(RefPtr<PeerContext> context, CheckedFormatString<Parameters...>&& fmtstr, Parameters const&... parameters)
    {
//        if (true) {
//            return;
//        }
        if (context.is_null())
            dbgln(move(fmtstr), parameters...);
        else
            dbgln("[{:21}] {}", context->address, String::formatted(move(fmtstr), parameters...).value());
    }

    // dbgln with a sub context... TODO: Better use a stack of contexts in global variable?
    template<typename... Parameters>
    void dbglncc(RefPtr<PeerContext> parent_context, NonnullRefPtr<PeerContext> sub_context, CheckedFormatString<Parameters...>&& fmtstr, Parameters const&... parameters)
    {
//        if (true) {
//            return;
//        }
        if (parent_context.is_null()){
            dbglnc(sub_context, move(fmtstr), parameters...);}
        else{
            dbgln("[{:21}][{:21}] {}", parent_context->address, sub_context->address, String::formatted(move(fmtstr), parameters...).value());}
    }
};

}
