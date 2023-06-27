/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "PeerContext.h"
#include "Peer.h"
#include "TorrentContext.h"

namespace Bits {

PeerContext::PeerContext(NonnullRefPtr<Peer> peer, ConnectionId connection_id, PeerId id)
    : peer(peer)
    , connection_id(connection_id)
    , id(id)
    , bitfield(peer->torrent_context->piece_count)
{
}

}

template<>
struct AK::Formatter<Bits::PeerContext> : AK::Formatter<FormatString> {
    ErrorOr<void> format(FormatBuilder& builder, Bits::PeerContext const& value)
    {
        return Formatter<FormatString>::format(builder, "{}"sv, value.peer);
    }
};