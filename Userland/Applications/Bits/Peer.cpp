/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Peer.h"

namespace Bits {

Peer::Peer(IPv4Address address, u16 port)
    : m_address(move(address))
    , m_port(port)
{
}

}