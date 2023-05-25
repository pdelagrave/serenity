/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Peer.h"

namespace Bits {

Peer::Peer(ByteBuffer const& id, IPv4Address const& address, u16 const port)
    : m_id(move(id))
    , m_address(move(address))
    , m_port(port)
{
}

}