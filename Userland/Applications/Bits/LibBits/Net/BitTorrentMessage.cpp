/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "BitTorrentMessage.h"

namespace Bits {

DeprecatedString Message::to_string(Type type)
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

DeprecatedString Message::to_string() const
{
    return to_string(type);
}

DeprecatedString BitFieldMessage::to_string() const
{
    return DeprecatedString::formatted("BitField: bytes:{} size:{} ones:{} zeroes:{} progress:{:.1}%", bitfield.bytes(), bitfield.size(), bitfield.ones(), bitfield.zeroes(), bitfield.progress());
}

DeprecatedString HandshakeMessage::to_string() const
{
    return DeprecatedString::formatted("Handshake: Protocol: {}, Reserved: {:08b} {:08b} {:08b} {:08b} {:08b} {:08b} {:08b} {:08b}, info_hash: {:20hex-dump}, peer_id: {:20hex-dump}",
        pstr,
        reserved[0],
        reserved[1],
        reserved[2],
        reserved[3],
        reserved[4],
        reserved[5],
        reserved[6],
        reserved[7],
        info_hash,
        peer_id);
}

DeprecatedString HaveMessage::to_string() const
{
    return DeprecatedString::formatted("Have: piece:{}", piece_index);
}

DeprecatedString PieceMessage::to_string() const
{
    return DeprecatedString::formatted("Piece: piece:{} offset:{} blocksize:{}", piece_index, begin_offset, block.size());
}

DeprecatedString RequestMessage::to_string() const
{
    return DeprecatedString::formatted("Request: piece:{} offset:{} blocksize:{}", piece_index, piece_offset, block_length);
}

}