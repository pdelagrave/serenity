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

class Peer : public RefCounted<Peer> {
public:
    Peer(IPv4Address address, u16 port);
    Optional<ReadonlyBytes> id() { return m_id->bytes(); }
    IPv4Address const& address() const { return m_address; }
    u16 port() const { return m_port; }

private:
    Optional<ByteBuffer> m_id;
    const IPv4Address m_address;
    const u16 m_port;
};

}

template<>
struct AK::Formatter<Bits::Peer> : AK::Formatter<StringView> {
    ErrorOr<void> format(FormatBuilder& builder, Bits::Peer const& value)
    {
        return Formatter<StringView>::format(builder, DeprecatedString::formatted("{}:{}", value.address(), value.port()));
    }
};

