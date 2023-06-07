/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "BitTorrentMessage.h"

namespace Bits::BitTorrent::Message {

ErrorOr<Handshake> Handshake::read_from_stream(Stream& stream)
{
    Handshake handshake;
    TRY(stream.read_until_filled(Bytes { &handshake, sizeof(Handshake) }));
    return handshake;
}

ErrorOr<DeprecatedString> to_string(Type type)
{
    switch (type) {
    case Type::Choke:
        return "Choke";
    case Type::Unchoke:
        return "Unchoke";
    case Type::Interested:
        return "Interested";
    case Type::NotInterested:
        return "NotInterested";
    case Type::Have:
        return "Have";
    case Type::Bitfield:
        return "Bitfield";
    case Type::Request:
        return "Request";
    case Type::Piece:
        return "Piece";
    case Type::Cancel:
        return "Cancel";
    default:
        return DeprecatedString::formatted("ERROR: unknown message type {}", (u8)type);
    }
}

template<typename... Payload>
static ErrorOr<ByteBuffer> serialize(Type message_type, Payload... payloads)
{
    // TODO make this more efficient?

    auto stream = AllocatingMemoryStream();

    TRY(stream.write_value(static_cast<u8>(message_type)));
    for (auto const& param : { payloads... }) {
        TRY(stream.write_value(param));
    }
    auto buffer = TRY(ByteBuffer::create_zeroed(stream.used_buffer_size()));
    TRY(stream.read_until_filled(buffer.bytes()));
    return buffer;
}

ErrorOr<ByteBuffer> bitfield(ReadonlyBytes bitfield)
{
    return serialize(Type::Bitfield, StreamWritable(bitfield));
}

ErrorOr<ByteBuffer> interested()
{
    return serialize(Type::Interested, StreamWritable({}));
}

ErrorOr<ByteBuffer> have(BigEndian<u32> piece_index)
{
    return serialize(Type::Have, piece_index);
}

ErrorOr<ByteBuffer> not_interested()
{
    return serialize(Type::NotInterested, StreamWritable({}));
}

ErrorOr<ByteBuffer> request(BigEndian<u32> piece_index, BigEndian<u32> piece_offset, BigEndian<u32> block_length)
{
    return serialize(Type::Request, piece_index, piece_offset, block_length);
}

ErrorOr<ByteBuffer> unchoke()
{
    return serialize(Type::Unchoke, StreamWritable({}));
}

}