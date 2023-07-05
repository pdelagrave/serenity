/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "FixedSizeByteString.h"
#include <AK/RefCounted.h>
#include <LibCore/SocketAddress.h>

namespace Bits {

enum class PeerStatus {
    Available,
    InUse,
    Errored
};

struct Torrent;

struct Peer : public RefCounted<Peer> {
    Peer(Core::SocketAddress address, NonnullRefPtr<Torrent> torrent);

    const Core::SocketAddress address;
    NonnullRefPtr<Torrent> const torrent;
    PeerStatus status = PeerStatus::Available;

    // FIXME ugly hack, should not be used to temporarily save the id before creating the peercontext.
    Optional<PeerId> id_from_handshake;

    static DeprecatedString status_string(PeerStatus);
};

}

template<>
struct AK::Formatter<Bits::Peer> : AK::Formatter<FormatString> {
    ErrorOr<void> format(FormatBuilder& builder, Bits::Peer const& value)
    {
        return Formatter<FormatString>::format(builder, "{}"sv, value.address.to_deprecated_string());
    }
};