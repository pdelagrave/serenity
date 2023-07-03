/*
* Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
*
* SPDX-License-Identifier: BSD-2-Clause
*/

#include "Peer.h"
#include "Torrent.h"
#include "PeerContext.h"

namespace Bits {

Peer::Peer(Core::SocketAddress address, NonnullRefPtr<Torrent> torrent)
    : address(move(address))
    , torrent(move(torrent))
{
}

}
