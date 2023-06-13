/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "BitTorrentMessage.h"

namespace Bits::BitTorrent {

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

DeprecatedString Have::to_string() const
{
    return DeprecatedString::formatted("Have: piece:{}", piece_index);
}

DeprecatedString Piece::to_string() const
{
    return DeprecatedString::formatted("Piece: piece:{} offset:{} blocksize:{}", piece_index, begin_offset, block.size());
}

DeprecatedString Request::to_string() const
{
    return DeprecatedString::formatted("Request: piece:{} offset:{} blocksize:{}", piece_index, piece_offset, block_length);
}

}