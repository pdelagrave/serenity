/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "Applications/Bits/Data/Command.h"
#include "Peer.h"
#include "Torrent.h"
#include <LibThreading/Mutex.h>

namespace Bits {

struct BittorrentHandshake {
    u8 pstrlen;
    u8 pstr[19];
    u8 reserved[8];
    u8 info_hash[20];
    u8 peer_id[20];

public:
    BittorrentHandshake(ReadonlyBytes info_hash, ReadonlyBytes peer_id)
        : pstrlen(19)
    {
        VERIFY(info_hash.size() == 20);
        VERIFY(peer_id.size() == 20);
        memcpy(pstr, "BitTorrent protocol", 19);
        memset(reserved, 0, 8);
        memcpy(this->info_hash, info_hash.data(), 20);
        memcpy(this->peer_id, peer_id.data(), 20);
    }
    BittorrentHandshake() = default;
    static ErrorOr<BittorrentHandshake> read_from_stream(Stream&);
};

enum DataEventType {
    AddConnection,
};

class Data : public Core::Object {
    C_OBJECT(Data);

public:
    Data();
    ~Data();
    void add_connection(NonnullRefPtr<Peer>, NonnullRefPtr<Torrent> torrent);
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

    struct SocketContext {
        Core::TCPSocket* socket;
        NonnullRefPtr<Peer> peer;
        NonnullRefPtr<Torrent> torrent;
        CircularBuffer outgoing_message_buffer;
        RefPtr<Core::Notifier> socket_writable_notifier {};
        bool sent_handshake = false;
        bool got_handshake = false;
        u32 incoming_message_length = sizeof(BittorrentHandshake);
        ByteBuffer incoming_message_buffer {};
        bool connected = false;
    };

    Queue<NonnullOwnPtr<SocketContext>> m_sockets_to_create;
    Threading::Mutex m_sockets_to_create_mutex;
    HashMap<Core::TCPSocket*, SocketContext*> m_socket_contexts;
    HashMap<NonnullRefPtr<Peer>, SocketContext*> m_peer_to_socket_context;
    ErrorOr<void> read_handshake(Stream& bytes, SocketContext* context);
    ErrorOr<void> add_new_connections();
    ErrorOr<void> read_from_socket(SocketContext* context);
    ErrorOr<void> send_local_bitfield(Core::TCPSocket*, SocketContext*);
    ErrorOr<bool> update_piece_availability(u64 piece_index, SocketContext* context);
    ErrorOr<void> receive_bitfield(ReadonlyBytes const&, SocketContext*);
    ErrorOr<void> insert_piece_in_heap(NonnullRefPtr<Torrent> torrent, u64 piece_index);
    ErrorOr<void> handle_piece_downloaded(Bits::PieceDownloadedCommand const& command);
    ErrorOr<void> handle_have(SocketContext* context, Stream& stream);
    ErrorOr<void> piece_or_peer_availability_updated(NonnullRefPtr<Torrent>& torrent);
    ErrorOr<void> send_message(ByteBuffer const&, SocketContext*);
    ErrorOr<void> flush_output_buffer(SocketContext* context);
};

}
