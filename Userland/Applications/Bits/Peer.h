/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "AK/OwnPtr.h"
#include "BitField.h"
#include "Torrent.h"
#include <AK/IPv4Address.h>
#include <AK/Types.h>
#include <AK/HashMap.h>

namespace Bits {
class PieceHeap;
struct PieceStatus;

class Peer : public RefCounted<Peer> {
public:
    Peer(IPv4Address address, u16 port);
    Optional<ReadonlyBytes> id() { return m_id->bytes(); }
    IPv4Address const& address() const { return m_address; }
    u16 port() const { return m_port; }

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

    auto& incoming_piece() { return m_incoming_piece; }
    auto& interesting_pieces() { return m_interesting_pieces; }

private:
    Optional<ByteBuffer> m_id;
    const IPv4Address m_address;
    const u16 m_port;
    BitField m_bitfield = {0};

    // long variable names because it gets confusing easily.
    bool m_peer_is_choking_us { true };
    bool m_peer_is_interested_in_us { false };
    bool m_we_are_choking_peer { true };
    bool m_we_are_interested_in_peer { false };

    HashMap<u64, nullptr_t> m_interesting_pieces;

    struct {
        ByteBuffer data;
        Optional<size_t> index;
        size_t offset;
        size_t length;
    } m_incoming_piece;
};

}

template<>
struct AK::Formatter<Bits::Peer> : AK::Formatter<StringView> {
    ErrorOr<void> format(FormatBuilder& builder, Bits::Peer const& value)
    {
        return Formatter<StringView>::format(builder, DeprecatedString::formatted("{}:{}", value.address(), value.port()));
    }
};

