/*
* Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
*
* SPDX-License-Identifier: BSD-2-Clause
*/

#include "Peer.h"
#include "TorrentContext.h"
#include "PeerContext.h"

namespace Bits {

Peer::Peer(Core::SocketAddress address, NonnullRefPtr<TorrentContext> tcontext)
    : address(move(address))
    , torrent_context(move(tcontext))
{
}

}
