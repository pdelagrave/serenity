/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "Peer.h"
#include "Torrent.h"
#include <LibThreading/Mutex.h>

namespace Bits {

enum class MessageType : u8 {
    Choke = 0x00,
    Unchoke = 0x01,
    Interested = 0x02,
    NotInterest = 0x03,
    Have = 0x04,
    Bitfield = 0x05,
    Request = 0x06,
    Piece = 0x07,
    Cancel = 0x08,
};

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
    void add_connection(NonnullRefPtr<Peer>, NonnullRefPtr<Torrent> torrent);

protected:
    void custom_event(Core::CustomEvent& event) override;

private:
    OwnPtr<Core::EventLoop> m_event_loop;
    RefPtr<Threading::Thread> m_thread;

    struct SocketContext {
        NonnullRefPtr<Peer> peer;
        NonnullRefPtr<Torrent> torrent;
        bool got_handshake = false;
        u32 incoming_message_length = sizeof(BittorrentHandshake);
    };

    Queue<NonnullOwnPtr<SocketContext>> m_sockets_to_create;
    Threading::Mutex m_sockets_to_create_mutex;
    HashMap<Core::TCPSocket*, NonnullOwnPtr<SocketContext>> m_socket_contexts;
    ErrorOr<void> read_handshake(Core::TCPSocket* socket, SocketContext& context);
    ErrorOr<void> add_new_connections();
    ErrorOr<void> read_from_socket(Core::TCPSocket*);
};

}
