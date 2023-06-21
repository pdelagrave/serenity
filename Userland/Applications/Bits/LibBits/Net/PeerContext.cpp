/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "PeerContext.h"
#include "TorrentContext.h"

namespace Bits {

ErrorOr<NonnullRefPtr<PeerConnection>> PeerConnection::try_create(NonnullOwnPtr<Core::TCPSocket>& socket, NonnullRefPtr<Core::Notifier> write_notifier, size_t input_buffer_size, size_t output_buffer_size, PeerRole role)
{
    auto input_buffer = TRY(CircularBuffer::create_empty(input_buffer_size));
    auto output_buffer = TRY(CircularBuffer::create_empty(output_buffer_size));
    return adopt_nonnull_ref_or_enomem(new (nothrow) PeerConnection(socket, write_notifier, input_buffer, output_buffer, role));
}

PeerConnection::PeerConnection(NonnullOwnPtr<Core::TCPSocket>& socket, NonnullRefPtr<Core::Notifier>& write_notifier, CircularBuffer& input_message_buffer, CircularBuffer& output_message_buffer, PeerRole role)
    : socket(move(socket))
    , socket_writable_notifier(move(write_notifier))
    , input_message_buffer(move(input_message_buffer))
    , output_message_buffer(move(output_message_buffer))
    ,role(role)
{
}

PeerContext::PeerContext(NonnullRefPtr<TorrentContext> tcontext, Core::SocketAddress address)
    : torrent_context(move(tcontext))
    , address(move(address))
    , bitfield(torrent_context->piece_count)
    , id("aaaaaaaaaaaaaaaaaaaa"sv.bytes())
{
}

}