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
}