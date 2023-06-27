/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "BitTorrentMessage.h"
#include "Net/Connection.h"
#include <AK/RefCounted.h>
#include <LibCore/DateTime.h>
#include <LibCore/Socket.h>

namespace Bits {

struct TorrentContext;
struct Peer;

struct PeerContext : public RefCounted<PeerContext> {
    PeerContext(NonnullRefPtr<Peer> peer, ConnectionId connection_id, PeerId id);

    const NonnullRefPtr<Peer> peer;
    const ConnectionId connection_id;
    const PeerId id;

    bool active = false;

    // long variable names because it gets confusing easily.
    bool peer_is_choking_us { true };
    bool peer_is_interested_in_us { false };
    bool we_are_choking_peer { true };
    bool we_are_interested_in_peer { false };

    BitField bitfield;
    HashTable<u64> interesting_pieces;

    // TODO: move this to PieceStatus?
    struct {
        ByteBuffer data;
        Optional<size_t> index;
        size_t offset;
        size_t length;
    } incoming_piece;
};

}
