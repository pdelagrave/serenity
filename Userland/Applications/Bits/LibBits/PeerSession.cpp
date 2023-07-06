/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "PeerSession.h"
#include "Peer.h"
#include "Torrent.h"

namespace Bits {

PeerSession::PeerSession(NonnullRefPtr<Peer> peer, ConnectionId connection_id, PeerId id)
    : peer(peer)
    , connection_id(connection_id)
    , id(id)
    , bitfield(peer->torrent->piece_count)
{
}

}

template<>
struct AK::Formatter<Bits::PeerSession> : AK::Formatter<FormatString> {
    ErrorOr<void> format(FormatBuilder& builder, Bits::PeerSession const& value)
    {
        return Formatter<FormatString>::format(builder, "{}"sv, value.peer);
    }
};