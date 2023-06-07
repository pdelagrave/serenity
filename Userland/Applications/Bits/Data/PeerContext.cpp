/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "PeerContext.h"

namespace Bits::Data {
ErrorOr<NonnullRefPtr<PeerContext>> PeerContext::try_create(NonnullRefPtr<Bits::Peer> peer, NonnullRefPtr<Bits::Torrent> torrent, size_t output_buffer_size)
{
    auto output_buffer = TRY(CircularBuffer::create_empty(output_buffer_size));
    return adopt_nonnull_ref_or_enomem(new (nothrow) PeerContext(peer, torrent, move(output_buffer)));
}
}