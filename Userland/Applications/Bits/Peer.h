/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/IPv4Address.h>
#include <AK/Types.h>
#include <LibCore/Socket.h>

namespace Bits {

class Peer {
public:
    Peer(ByteBuffer const& id, IPv4Address const& address, u16 const port);
    ReadonlyBytes get_id() { return m_id.bytes(); }
    IPv4Address const& get_address() { return m_address; }
    u16 get_port() { return m_port; }
    ByteBuffer const& get_bitfield() { return m_bitfield; }

private:
    const ByteBuffer m_id;
    const IPv4Address m_address;
    const u16 m_port;
    OwnPtr<Core::TCPSocket> m_socket;
    ByteBuffer m_bitfield;
};

}
