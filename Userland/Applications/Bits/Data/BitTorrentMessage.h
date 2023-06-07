/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "AK/MemoryStream.h"
#include <AK/DeprecatedString.h>
#include <AK/ByteBuffer.h>
#include <AK/Endian.h>
#include <AK/Types.h>
#include <AK/TypeCasts.h>
#include <initializer_list>

namespace Bits::BitTorrent::Message {

struct Handshake {
    u8 pstrlen;
    u8 pstr[19];
    u8 reserved[8];
    u8 info_hash[20];
    u8 peer_id[20];

public:
    Handshake(ReadonlyBytes info_hash, ReadonlyBytes peer_id)
        : pstrlen(19)
    {
        VERIFY(info_hash.size() == 20);
        VERIFY(peer_id.size() == 20);
        memcpy(pstr, "BitTorrent protocol", 19);
        memset(reserved, 0, 8);
        memcpy(this->info_hash, info_hash.data(), 20);
        memcpy(this->peer_id, peer_id.data(), 20);
    }
    Handshake() = default;
    static ErrorOr<Handshake> read_from_stream(Stream&);
};

enum class Type : u8 {
    Choke = 0x00,
    Unchoke = 0x01,
    Interested = 0x02,
    NotInterested = 0x03,
    Have = 0x04,
    Bitfield = 0x05,
    Request = 0x06,
    Piece = 0x07,
    Cancel = 0x08,
};

ErrorOr<DeprecatedString> to_string(Type);

class StreamWritable {
public:
    StreamWritable(ReadonlyBytes bytes)
        : m_bytes(bytes)
    {
    }

    ErrorOr<void> write_to_stream(AK::Stream& stream) const {
        return stream.write_until_depleted(m_bytes);
    }
private:
    ReadonlyBytes m_bytes;
};

ErrorOr<ByteBuffer> bitfield(ReadonlyBytes bitfield);
ErrorOr<ByteBuffer> interested();
ErrorOr<ByteBuffer> have(BigEndian<u32> piece_index);
ErrorOr<ByteBuffer> not_interested();
ErrorOr<ByteBuffer> request(BigEndian<u32> piece_index, BigEndian<u32> piece_offset, BigEndian<u32> block_length);
ErrorOr<ByteBuffer> unchoke();

}
