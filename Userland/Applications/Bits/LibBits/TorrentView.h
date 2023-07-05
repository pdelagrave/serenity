/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "FixedSizeByteString.h"
#include "Torrent.h"
#include <AK/DeprecatedString.h>

namespace Bits {

struct PeerView {
    PeerId peer_id;
    DeprecatedString ip;
    u16 port;
    float progress;
    u64 download_speed;
    u64 upload_speed;
    u64 downloaded_bytes;
    u64 uploaded_bytes;
    bool we_choking_it;
    bool it_choking_us;
    bool we_interested;
    bool it_interested;
    bool connected;
};

struct TorrentView {
    InfoHash info_hash;
    DeprecatedString display_name;
    u64 size;
    TorrentState state;
    float progress;
    float check_progress;
    u64 download_speed;
    u64 upload_speed;
    DeprecatedString save_path;
    Vector<PeerView> peers;
    BitField bitfield;
};

}