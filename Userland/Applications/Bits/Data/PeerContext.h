/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "BitTorrentMessage.h"
#include <AK/RefCounted.h>
#include <LibCore/DateTime.h>
#include <LibCore/Socket.h>

namespace Bits::Data {

struct TorrentContext;

struct PeerContext : public RefCounted<PeerContext> {
    PeerContext(NonnullRefPtr<TorrentContext> tcontext, Core::SocketAddress address, CircularBuffer input_message_buffer, CircularBuffer output_message_buffer);
    static NonnullRefPtr<PeerContext> create(NonnullRefPtr<TorrentContext> tcontext, Core::SocketAddress address, size_t input_buffer_size, size_t output_buffer_size);

    const NonnullRefPtr<TorrentContext> torrent_context;
    const Core::SocketAddress address;

    bool got_handshake = false;
    // TODO: Make this into a PeerContextState enum?
    bool connected = false;
    bool active = false;
    bool errored = false;

    // long variable names because it gets confusing easily.
    bool peer_is_choking_us { true };
    bool peer_is_interested_in_us { false };
    bool we_are_choking_peer { true };
    bool we_are_interested_in_peer { false };

    BitField bitfield {0};
    HashTable<u64> interesting_pieces;

    // TODO: move this to PieceStatus?
    struct {
        ByteBuffer data;
        Optional<size_t> index;
        size_t offset;
        size_t length;
    } incoming_piece;

    BigEndian<u32> incoming_message_length = sizeof(BitTorrent::Handshake);
    CircularBuffer input_message_buffer;

    u64 bytes_downloaded_since_last_speed_measurement { 0 };
    u64 download_speed { 0 };

    u64 bytes_uploaded_since_last_speed_measurement { 0 };
    u64 upload_speed { 0 };

    Core::DateTime last_message_received_at;
    Core::DateTime last_message_sent_at;

    CircularBuffer output_message_buffer;
    RefPtr<Core::Notifier> socket_writable_notifier {};
    OwnPtr<Core::TCPSocket> socket {};
};

}

template<>
struct AK::Formatter<Bits::Data::PeerContext> : AK::Formatter<StringView> {
    ErrorOr<void> format(FormatBuilder& builder, Bits::Data::PeerContext const& value)
    {
        return Formatter<StringView>::format(builder, DeprecatedString::formatted("{}", value.address));
    }
};
