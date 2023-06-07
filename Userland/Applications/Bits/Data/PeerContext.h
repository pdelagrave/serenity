/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../Peer.h"
#include "BitTorrentMessage.h"
#include <AK/RefCounted.h>
#include <LibCore/Socket.h>

namespace Bits::Data {

struct PeerContext : public RefCounted<PeerContext> {
    PeerContext(NonnullRefPtr<Peer> peer, NonnullRefPtr<Torrent> torrent, CircularBuffer output_message_buffer)
        : peer(move(peer))
        , torrent(move(torrent))
        , output_message_buffer(move(output_message_buffer))

    {
    }
    NonnullRefPtr<Peer> peer;
    NonnullRefPtr<Torrent> torrent;
    CircularBuffer output_message_buffer;

    OwnPtr<Core::TCPSocket> socket {};
    RefPtr<Core::Notifier> socket_writable_notifier {};
    bool connected = false;

    bool sent_handshake = false;
    bool got_handshake = false;

    u32 incoming_message_length = sizeof(BitTorrent::Handshake);
    ByteBuffer incoming_message_buffer {};

    static ErrorOr<NonnullRefPtr<PeerContext>> try_create(NonnullRefPtr<Peer> peer, NonnullRefPtr<Torrent> torrent, size_t output_buffer_size);
};

}
