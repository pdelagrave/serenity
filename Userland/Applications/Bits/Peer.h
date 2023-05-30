/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "BitField.h"
#include <AK/IPv4Address.h>
#include <AK/Types.h>

namespace Bits {

class Peer : public RefCounted<Peer> {
public:
    Peer(ByteBuffer const& id, IPv4Address const& address, u16 const port);
    ReadonlyBytes id() { return m_id.bytes(); }
    IPv4Address const& address() { return m_address; }
    u16 port() { return m_port; }

    BitField& bitfield() { return m_bitfield; }
    void set_bitbield(BitField const& bitfield) { m_bitfield = bitfield; }
    bool is_choking_us() { return m_peer_is_choking_us; }
    bool is_interested_in_us() { return m_peer_is_interested_in_us; }
    bool is_choking_peer() { return m_we_are_choking_peer; }
    bool is_interested_in_peer() { return m_we_are_interested_in_peer; }

    void set_peer_is_choking_us(bool const value) { m_peer_is_choking_us = value; }
    void set_peer_is_interested_in_us(bool const value) { m_peer_is_interested_in_us = value; }
    void set_choking_peer(bool const value) { m_we_are_choking_peer = value; }
    void set_interested_in_peer(bool const value) { m_we_are_interested_in_peer = value; }

private:
    const ByteBuffer m_id;
    const IPv4Address m_address;
    const u16 m_port;
    BitField m_bitfield = {0};

    // long variable names because it gets confusing easily.
    bool m_peer_is_choking_us { true };
    bool m_peer_is_interested_in_us { false };
    bool m_we_are_choking_peer { true };
    bool m_we_are_interested_in_peer { false };
};

}
