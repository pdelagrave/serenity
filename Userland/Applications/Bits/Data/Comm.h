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

enum DataEventType {
    AddConnection,
};

class Comm : public Core::Object {
    C_OBJECT(Comm);

public:
    Comm();
    ErrorOr<void> add_connection(NonnullRefPtr<Peer>, NonnullRefPtr<Torrent> torrent);
    virtual void event(Core::Event& event) override;
    u16 max_active_peers = 10;

protected:
    void custom_event(Core::CustomEvent& event) override;

private:
    const u64 BlockLength = 16 * KiB;
    OwnPtr<Core::EventLoop> m_event_loop;
    RefPtr<Threading::Thread> m_thread;

    // TODO: Use a HashSet or the like instead
    HashMap<NonnullRefPtr<Peer>, nullptr_t> m_active_peers;

    Queue<NonnullRefPtr<PeerContext>> m_sockets_to_create;
    Threading::Mutex m_sockets_to_create_mutex;
    HashMap<NonnullRefPtr<Peer>, NonnullRefPtr<PeerContext>> m_peer_to_context;
    HashMap<NonnullRefPtr<Torrent>, NonnullRefPtr<PeerContext>> m_torrent_to_context;
    ErrorOr<void> read_handshake(Stream& bytes, NonnullRefPtr<PeerContext> context);
    ErrorOr<void> add_new_connections();
    ErrorOr<void> read_from_socket(NonnullRefPtr<PeerContext> context);
    ErrorOr<bool> update_piece_availability(u64 piece_index, NonnullRefPtr<PeerContext> context);
    ErrorOr<void> receive_bitfield(ReadonlyBytes const&, NonnullRefPtr<PeerContext>);
    ErrorOr<void> insert_piece_in_heap(NonnullRefPtr<Torrent> torrent, u64 piece_index);
    ErrorOr<void> handle_piece_downloaded(PieceDownloadedCommand const& command);
    ErrorOr<void> handle_have(NonnullRefPtr<PeerContext> context, Stream& stream);
    ErrorOr<void> piece_or_peer_availability_updated(NonnullRefPtr<PeerContext> context);
    ErrorOr<void> send_message(ByteBuffer const&, NonnullRefPtr<PeerContext> context, RefPtr<PeerContext> parent_context = {});
    ErrorOr<void> flush_output_buffer(NonnullRefPtr<PeerContext> context, RefPtr<PeerContext> parent_context = {});

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
