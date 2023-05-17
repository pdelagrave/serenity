/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/IPv4Address.h>
#include <AK/Types.h>

namespace Bits {

class Peer {
public:
    Peer(ByteBuffer const& id, IPv4Address const& address, u16 const port);
    ReadonlyBytes get_id() { return id.bytes(); }
    const IPv4Address& get_address() { return address; }
    u16 get_port() { return port; }

private:
    const ByteBuffer id;
    const IPv4Address address;
    const u16 port;
};

}
