/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../Peer.h"
#include "../Torrent.h"
#include "Command.h"
#include "PeerContext.h"
#include <LibThreading/Mutex.h>

namespace Bits::Data {

class Comm : public Core::Object {
    C_OBJECT(Comm);

public:
    Comm();
    ErrorOr<void> add_connection(NonnullRefPtr<Peer>, NonnullRefPtr<Torrent> torrent);
    u16 max_active_peers = 10;

protected:
    void custom_event(Core::CustomEvent& event) override;

private:
    const u64 BlockLength = 16 * KiB;
    OwnPtr<Core::EventLoop> m_event_loop;
    RefPtr<Threading::Thread> m_thread;

    HashTable<NonnullRefPtr<Peer>> m_active_peers;
    HashMap<NonnullRefPtr<Peer>, NonnullRefPtr<PeerContext>> m_peer_to_context;
    HashMap<NonnullRefPtr<Torrent>, NonnullRefPtr<PeerContext>> m_torrent_to_context;

    // Bits Engine/Comm internal commands
    ErrorOr<void> post_command(NonnullOwnPtr<Command> command);
    ErrorOr<void> handle_command_add_peer(AddPeerCommand const& command);
    ErrorOr<void> handle_command_piece_downloaded(PieceDownloadedCommand const& command);

    // Comm BT message handlers
    ErrorOr<void> handle_handshake(Stream& bytes, NonnullRefPtr<PeerContext> context);
    ErrorOr<void> handle_bitfield(ReadonlyBytes const&, NonnullRefPtr<PeerContext>);
    ErrorOr<void> handle_have(NonnullRefPtr<PeerContext> context, Stream& stream);

    // Comm BT low level network functions
    ErrorOr<void> read_from_socket(NonnullRefPtr<PeerContext> context);
    ErrorOr<void> send_message(ByteBuffer const&, NonnullRefPtr<PeerContext> context, RefPtr<PeerContext> parent_context = {});
    ErrorOr<void> flush_output_buffer(NonnullRefPtr<PeerContext> context, RefPtr<PeerContext> parent_context = {});

    // BT higher level logic
    ErrorOr<void> piece_or_peer_availability_updated(NonnullRefPtr<PeerContext> context);
    ErrorOr<bool> update_piece_availability(u64 piece_index, NonnullRefPtr<PeerContext> context);
    ErrorOr<void> insert_piece_in_heap(NonnullRefPtr<Torrent> torrent, u64 piece_index);

    // dbgln with Context
    template<typename... Parameters>
    void dbglnc(NonnullRefPtr<PeerContext> context, CheckedFormatString<Parameters...>&& fmtstr, Parameters const&... parameters)
    {
        dbgln("[{:21}] {}", context->peer, String::formatted(move(fmtstr), parameters...).value());
    }

    // dbgln with a sub context... TODO: Better use a stack of contexts in global variable?
    template<typename... Parameters>
    void dbglncc(RefPtr<PeerContext> parent_context, NonnullRefPtr<PeerContext> sub_context, CheckedFormatString<Parameters...>&& fmtstr, Parameters const&... parameters)
    {
        if (parent_context.is_null())
            dbglnc(sub_context, move(fmtstr), parameters...);
        else
            dbgln("[{:21}][{:21}] {}", parent_context->peer, sub_context->peer, String::formatted(move(fmtstr), parameters...).value());
    }
};

}
