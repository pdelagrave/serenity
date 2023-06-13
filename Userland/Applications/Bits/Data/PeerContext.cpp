/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "PeerContext.h"
#include "TorrentContext.h"

namespace Bits::Data {

PeerContext::PeerContext(NonnullRefPtr<TorrentContext> tcontext, Core::SocketAddress address, CircularBuffer input_message_buffer, CircularBuffer output_message_buffer)
    : torrent_context(move(tcontext))
    , address(move(address))
    , input_message_buffer(move(input_message_buffer))
    , output_message_buffer(move(output_message_buffer))

{
}

NonnullRefPtr<PeerContext> PeerContext::create(NonnullRefPtr<TorrentContext> tcontext, Core::SocketAddress address, size_t input_buffer_size, size_t output_buffer_size)
{
    auto input_buffer = CircularBuffer::create_empty(input_buffer_size).release_value();
    auto output_buffer = CircularBuffer::create_empty(output_buffer_size).release_value();
    return adopt_nonnull_ref_or_enomem(new (nothrow) PeerContext(tcontext, move(address), move(input_buffer), move(output_buffer))).release_value();
}

}