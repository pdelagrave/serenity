/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Comm.h"
#include "BitTorrentMessage.h"
#include "Command.h"
#include "PeerContext.h"
#include <LibCore/System.h>

namespace Bits::Data {

Comm::Comm()
{
    m_thread = Threading::Thread::construct([this]() -> intptr_t {
        m_event_loop = make<Core::EventLoop>();
        return m_event_loop->exec();
    },
        "Data thread"sv);
    m_thread->start();
}

ErrorOr<void> Comm::add_connection(NonnullRefPtr<Peer> peer, NonnullRefPtr<Torrent> torrent)
{
    return TRY(post_command(make<AddPeerCommand>(torrent, peer)));
}

void Comm::custom_event(Core::CustomEvent& event)
{
    auto err = [&]() -> ErrorOr<void> {
        switch (static_cast<Command::Type>(event.custom_type())) {
        case Command::Type::AddPeer:
            return handle_command_add_peer(static_cast<AddPeerCommand const&>(event));
        case Command::Type::PieceDownloaded:
            return handle_command_piece_downloaded(static_cast<PieceDownloadedCommand const&>(event));
        default:
            Object::event(event);
            break;
        }
        return {};
    }();
    if (err.is_error())
        dbgln("Error handling event: {}", err.error());
}

ErrorOr<void> Comm::post_command(NonnullOwnPtr<Command> command)
{
    m_event_loop->post_event(*this, move(command));
    return {};
}

ErrorOr<void> Comm::handle_command_add_peer(AddPeerCommand const& command)
{
    auto context = TRY(PeerContext::try_create(command.peer, command.torrent, 1 * MiB));
    auto address = Core::SocketAddress { context->peer->address(), context->peer->port() };
    auto socket_fd = TRY(Core::System::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0));
    auto sockaddr = address.to_sockaddr_in();
    auto connect_err = Core::System::connect(socket_fd, bit_cast<struct sockaddr*>(&sockaddr), sizeof(sockaddr));
    if (connect_err.is_error() && connect_err.error().code() != EINPROGRESS) {
        dbglnc(context, "Error connecting to peer {}: {}", address.to_deprecated_string(), connect_err.error());
        return {};
    }

    context->socket_writable_notifier = Core::Notifier::construct(socket_fd, Core::Notifier::Type::Write);

    auto socket = TRY(Core::TCPSocket::adopt_fd(socket_fd));
    context->socket = move(socket);

    m_peer_to_context.set(context->peer, context);

    context->socket_writable_notifier->on_activation = [&, socket_fd, context, address] {
        if (context->connected) {
            VERIFY(context->output_message_buffer.used_space() > 0);
            auto err = flush_output_buffer(context);
            if (err.is_error()) {
                dbglnc(context, "{} error flushing output buffer: {}", context->peer, err.error());
            }
            return;
        }
        int so_error;
        socklen_t len = sizeof(so_error);
        auto ret = getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &so_error, &len);
        if (ret == -1) {
            auto errn = errno;
            dbglnc(context, "error calling getsockopt: errno:{} {}", errn, strerror(errn));
            return;
        }
        // dbglnc(context, "getsockopt SO_ERROR resut: '{}' ({}) for peer {}", strerror(so_error), so_error, address.to_deprecated_string());

        if (so_error == ECONNREFUSED) {
            dbglnc(context, "Connection refused {}", context->peer);
            context->socket_writable_notifier->close();
            context->socket->close();
        } else if (so_error == ETIMEDOUT) {
            dbglnc(context, "Connection timed out {}", context->peer);
            context->socket_writable_notifier->close();
            context->socket->close();
        } else if (so_error == ESUCCESS) {
            dbglnc(context, "Connection succeeded {}, sending handshake", context->peer);
            context->connected = true;
            context->socket_writable_notifier->set_enabled(false);
            auto handshake = BitTorrent::Message::Handshake(context->torrent->meta_info().info_hash(), context->torrent->local_peer_id());
            context->socket->write_until_depleted({ &handshake, sizeof(handshake) }).release_value_but_fixme_should_propagate_errors();
            context->socket->on_ready_to_read = [&, context] {
                auto err = read_from_socket(context);
                if (err.is_error()) {
                    dbglnc(context, "Error reading from socket: {} for peer {}", err.error(), context->peer);
                    return;
                }
            };
        } else {
            dbglnc(context, "Unhandled error: '{}' ({}) for peer {}", strerror(so_error), so_error, context->peer);
            context->socket->close();
        }
    };
    return {};
}

ErrorOr<void> Comm::handle_command_piece_downloaded(PieceDownloadedCommand const& command)
{
    auto context = command.context();
    auto torrent = context->torrent;
    auto index = command.index();
    auto data = command.data();
    if (TRY(torrent->data_file_map()->validate_hash(index, data))) {
        TRY(torrent->data_file_map()->write_piece(index, data));

        torrent->local_bitfield().set(index, true);
        auto& havers = torrent->missing_pieces().get(index).value()->havers;
        dbglnc(context, "Havers of that piece {} we just downloaded: {}", index, havers.size());
        for (auto& haver_ : havers) {
            auto& haver = haver_.key;
            VERIFY(haver->interesting_pieces().remove(index));
            dbglnc(context, "Removed piece {} from interesting pieces of {}", index, haver);
            if (haver->interesting_pieces().is_empty()) {
                dbglnc(context, "Peer {} has no more interesting pieces, sending a NotInterested message", haver);
                TRY(send_message(TRY(BitTorrent::Message::not_interested()), *m_peer_to_context.get(haver).value()));
                haver->set_interested_in_peer(false);
            }
        }
        torrent->missing_pieces().remove(index);

        dbglnc(context, "We completed piece {}", index);

        for (auto sub_context : m_torrent_to_context) {
            if (torrent == sub_context.value->torrent) // TODO: create a hashmap for contexts per torrent
                TRY(send_message(TRY(BitTorrent::Message::have(index)), sub_context.value));
        }
    } else {
        TRY(insert_piece_in_heap(torrent, index));
        dbglnc(context, "Piece {} failed hash check", index);
    }
    TRY(piece_or_peer_availability_updated(context));
    return {};
}

ErrorOr<void> Comm::handle_handshake(Stream& bytes, NonnullRefPtr<PeerContext> context)
{
    auto handshake = TRY(bytes.read_value<BitTorrent::Message::Handshake>());
    dbglnc(context, "Received handshake_message: Protocol: {}, Reserved: {:08b} {:08b} {:08b} {:08b} {:08b} {:08b} {:08b} {:08b}, info_hash: {:20hex-dump}, peer_id: {:20hex-dump}",
        handshake.pstr,
        handshake.reserved[0],
        handshake.reserved[1],
        handshake.reserved[2],
        handshake.reserved[3],
        handshake.reserved[4],
        handshake.reserved[5],
        handshake.reserved[6],
        handshake.reserved[7],
        handshake.info_hash,
        handshake.peer_id);

    if (context->torrent->meta_info().info_hash() != Bytes { handshake.info_hash, 20 }) {
        dbglnc(context, "Peer sent a handshake with the wrong info hash.");
        return Error::from_string_literal("Peer sent a handshake with the wrong info hash.");
    }
    context->got_handshake = true;
    context->incoming_message_length = 0;

    return {};
}

ErrorOr<void> Comm::handle_bitfield(ReadonlyBytes const& bytes, NonnullRefPtr<PeerContext> context)
{
    auto bitfield_data_size = context->torrent->local_bitfield().data_size();
    if (bytes.size() != bitfield_data_size) {
        warnln("Bitfield sent by peer has a size ({}) different than expected({})", bytes.size(), bitfield_data_size);
        return Error::from_string_literal("Bitfield sent by peer has a size different than expected");
    }
    context->peer->set_bitbield(BitField(TRY(ByteBuffer::copy(bytes)), context->torrent->piece_count()));
    dbglnc(context, "Set bitfield for peer. size: {} data_size: {}", context->peer->bitfield().size(), context->peer->bitfield().data_size());

    for (auto& missing_piece : context->torrent->missing_pieces()) {
        if (context->peer->bitfield().get(missing_piece.key))
            TRY(update_piece_availability(missing_piece.key, context));
    }
    return {};
}

ErrorOr<void> Comm::handle_have(NonnullRefPtr<PeerContext> context, AK::Stream& stream)
{
    auto piece_index = TRY(stream.read_value<BigEndian<u32>>());
    dbglnc(context, "Peer {} has piece {}, setting in peer bitfield, bitfield size: {}", context->peer->address().to_deprecated_string(), piece_index, context->peer->bitfield().size());
    context->peer->bitfield().set(piece_index, true);
    TRY(update_piece_availability(piece_index, context));
    return {};
}

ErrorOr<void> Comm::read_from_socket(NonnullRefPtr<PeerContext> context)
{
    // TODO: cleanup this mess, read everything we can in a buffer at every call first.
    auto& peer = context->peer;
    auto& socket = context->socket;
    // dbglnc(context, "Reading from socket");

    // hack:
    if (TRY(socket->pending_bytes()) == 0) {
        dbglnc(context, "Socket has no pending bytes, reading and writing to it to force receiving an RST");
        // remote host probably closed the connection, reading from the socket to force receiving an RST and having the connection being fully closed on our side.
        //        TRY(socket->read_some(ByteBuffer::create_uninitialized(1).release_value().bytes()));
        //        TRY(socket->write_value(BigEndian<u32>(0)));
        context->socket_writable_notifier->close();
        socket->close();
        return {};
    }
    if (TRY(socket->can_read_without_blocking())) {
        if (context->incoming_message_length == 0) {
            // dbglnc(context, "Incoming message length is 0");
            if (TRY(socket->pending_bytes()) >= 2) {
                // dbglnc(context, "Socket has at least 2 pending bytes");
                context->incoming_message_length = TRY(socket->read_value<BigEndian<u32>>());
                if (context->incoming_message_length == 0) {
                    dbglnc(context, "Received keep-alive");
                    return {};
                } else {
                    context->incoming_message_buffer.clear();
                    context->incoming_message_buffer.ensure_capacity(context->incoming_message_length);
                }
            } else {
                // dbglnc(context, "Socket has only {} pending bytes", TRY(socket->pending_bytes()));
                return {};
            }
        }

        auto pending_bytes = TRY(socket->pending_bytes());
        //        dbglnc(context, "Socket has {} pending bytes", pending_bytes);
        //        dbglnc(context, "Incoming message length is {}", context->incoming_message_length);
        //        dbglnc(context, "Incoming message buffer size is {}", context->incoming_message_buffer.size());
        auto will_read = min(pending_bytes, context->incoming_message_length - context->incoming_message_buffer.size());
        //        dbglnc(context, "Will read {} bytes", will_read);
        TRY(socket->read_until_filled(TRY(context->incoming_message_buffer.get_bytes_for_writing(will_read))));

        if (context->incoming_message_buffer.size() == context->incoming_message_length) {
            auto message_stream = FixedMemoryStream(context->incoming_message_buffer.bytes());
            if (!context->got_handshake) {
                dbglnc(context, "No handshake yet, trying to read and parse it");
                TRY(handle_handshake(message_stream, context));
                TRY(send_message(TRY(BitTorrent::Message::bitfield(context->torrent->local_bitfield().bytes())), context));
            } else {
                using MessageType = Bits::BitTorrent::Message::Type;
                auto message_id = TRY(message_stream.read_value<MessageType>());
                dbglnc(context, "Got message type {}", TRY(to_string(message_id)));
                context->incoming_message_length = 0;
                switch (message_id) {
                case MessageType::Choke:
                    peer->set_peer_is_choking_us(true);
                    TRY(piece_or_peer_availability_updated(context));
                    break;
                case MessageType::Unchoke:
                    peer->set_peer_is_choking_us(false);
                    TRY(piece_or_peer_availability_updated(context));
                    break;
                case MessageType::Interested:
                    peer->set_peer_is_interested_in_us(true);
                    break;
                case MessageType::NotInterested:
                    peer->set_peer_is_interested_in_us(false);
                    break;
                case MessageType::Have:
                    TRY(handle_have(context, message_stream));
                    break;
                case MessageType::Bitfield: {
                    TRY(handle_bitfield(message_stream.bytes().slice(1), context));
                    break;
                }
                case MessageType::Request:
                    dbglnc(context, "ERROR: Message type Request is unsupported");
                    break;
                case MessageType::Piece: {
                    auto block_size = TRY(message_stream.size()) - 9;
                    auto index = TRY(message_stream.read_value<BigEndian<u32>>());
                    auto begin = TRY(message_stream.read_value<BigEndian<u32>>());

                    auto& piece = peer->incoming_piece();
                    if (piece.index.has_value()) {
                        VERIFY(index == piece.index);
                        VERIFY(begin == piece.offset);
                    } else {
                        VERIFY(begin == 0);
                        piece.index = index;
                        piece.offset = 0;
                        piece.length = context->torrent->piece_length(index);
                        piece.data.resize(piece.length);
                    }

                    piece.data.overwrite(begin, message_stream.bytes().slice(9, block_size).data(), block_size);
                    piece.offset = begin + block_size;
                    if (piece.offset == (size_t)piece.length) {
                        TRY(post_command(make<PieceDownloadedCommand>(index, piece.data.bytes(), context)));
                        piece.index = {};
                        m_active_peers.remove(peer);
                    } else {
                        if (peer->is_choking_us()) {
                            dbglnc(context, "Weren't done downloading the blocks for this piece {}, but peer is choking us, so we're giving up on it", index);
                            piece.index = {};
                            m_active_peers.remove(peer);
                            TRY(insert_piece_in_heap(context->torrent, index));
                            TRY(piece_or_peer_availability_updated(context));
                        } else {
                            auto next_block_length = min((size_t)BlockLength, (size_t)piece.length - piece.offset);
                            // dbglnc(context, "Sending next request for piece {} at offset {}/{} blocklen: {}", index, piece.offset, piece.length, next_block_length);
                            TRY(send_message(TRY(BitTorrent::Message::request(index, piece.offset, next_block_length)), context));
                        }
                    }
                    break;
                }
                case MessageType::Cancel:
                    dbglnc(context, "ERROR: message type Cancel is unsupported");
                    break;
                default:
                    dbglnc(context, "ERROR: Got unsupported message type: {:02X}: {}", (u8)message_id, TRY(to_string(message_id)));
                    break;
                }
            }
        }
    }
    return {};
}

ErrorOr<void> Comm::send_message(ByteBuffer const& message, NonnullRefPtr<PeerContext> context, RefPtr<PeerContext> parent_context)
{
    size_t size_to_send = message.size() + sizeof(u32); // message size + message payload
    if (context->output_message_buffer.empty_space() < size_to_send) {
        dbglncc(parent_context, context, "{} Outgoing message buffer is full, dropping message", context->peer);
        return {};
    }
    BigEndian<u32> const& endian = BigEndian<u32>(message.size());
    context->output_message_buffer.write({ &endian, sizeof(u32) });
    context->output_message_buffer.write(message);
    return flush_output_buffer(context, parent_context);
}

ErrorOr<void> Comm::flush_output_buffer(NonnullRefPtr<PeerContext> context, RefPtr<PeerContext> parent_context)
{
    for (;;) {
        auto err = context->output_message_buffer.flush_to_stream(*context->socket);
        if (err.is_error()) {
            if (err.error().code() == EAGAIN || err.error().code() == EWOULDBLOCK || err.error().code() == EINTR) {
                dbglncc(parent_context, context, "Socket is not ready to write, enabling read to write notifier");
                context->socket_writable_notifier->set_enabled(true);
            } else {
                dbglncc(parent_context, context, "Error writing to socket: err: {}  code: {}  codestr: {}", err.error(), err.error().code(), strerror(err.error().code()));
            }
            return {};
        }
        dbglncc(parent_context, context, "Wrote {} bytes to socket", err.value());

        if (context->output_message_buffer.used_space() == 0) {
            dbglncc(parent_context, context, "Output message buffer is empty, we sent everything, disabling ready to write notifier");
            context->socket_writable_notifier->set_enabled(false);
            return {};
        }
    }
}

ErrorOr<void> Comm::piece_or_peer_availability_updated(NonnullRefPtr<PeerContext> context)
{
    auto torrent = context->torrent;
    size_t max_to_add = max_active_peers - m_active_peers.size();
    dbglnc(context, "We can add {} peers to download stuff ({}/{})", max_to_add, m_active_peers.size(), max_active_peers);
    for (size_t i = 0; i < max_to_add; i++) {
        dbglnc(context, "Trying to add a {}th peer to download stuff", i);
        if (torrent->piece_heap().is_empty())
            return {};

        auto next_piece_index = torrent->piece_heap().peek_min()->index_in_torrent;
        dbglnc(context, "Picked next piece for download {}", next_piece_index);
        // TODO improve how we select the peer. Choking algo, bandwidth, etc
        bool found_peer = false;
        for (auto& peer : torrent->missing_pieces().get(next_piece_index).value()->havers.keys()) {
            if (!m_peer_to_context.contains(peer)) // filter out peers we failed to connect with, gotta improve this.
                continue;

            dbglnc(context, "Peer {} is choking us: {}, is active: {}", peer, peer->is_choking_us(), m_active_peers.contains(peer));
            if (!peer->is_choking_us() && !m_active_peers.contains(peer)) {
                auto sub_context = m_peer_to_context.get(peer).value();
                dbglncc(context, *sub_context, "Requesting piece {} from peer {}", next_piece_index, peer);

                m_active_peers.set(peer);
                u32 block_length = min(BlockLength, torrent->piece_length(next_piece_index));
                TRY(send_message(TRY(BitTorrent::Message::request(next_piece_index, 0, block_length)), *sub_context, context));

                found_peer = true;
                break;
            }
        }
        if (found_peer) {
            dbglnc(context, "Found peer for piece {}, popping the piece from the heap", next_piece_index);
            auto piece_status = torrent->piece_heap().pop_min();
            piece_status->currently_downloading = true;
            VERIFY(piece_status->index_in_torrent == next_piece_index);
        } else {
            dbglnc(context, "No more available peer to download piece {}", next_piece_index);
            break;
        }
    }
    return {};
}

ErrorOr<bool> Comm::update_piece_availability(u64 piece_index, NonnullRefPtr<PeerContext> context)
{
    auto& torrent = context->torrent;
    auto& peer = context->peer;
    auto is_missing = torrent->missing_pieces().get(piece_index);
    if (!is_missing.has_value()) {
        dbglnc(context, "Peer has piece {} but we already have it.", piece_index);
        return false;
    }

    RefPtr<PieceStatus> piece_status = is_missing.value();
    piece_status->havers.set(peer, nullptr);
    if (!piece_status->currently_downloading) {
        auto& heap = torrent->piece_heap();
        if (piece_status->index_in_heap.has_value()) {
            heap.update(*piece_status);
        } else {
            heap.insert(*piece_status);
        }
    }

    peer->interesting_pieces().set(piece_index, nullptr);

    if (!peer->is_interested_in_peer()) {
        peer->set_interested_in_peer(true);

        // TODO: figure out when is the best time to unchoke the peer.
        TRY(send_message(TRY(BitTorrent::Message::unchoke()), context));
        peer->set_choking_peer(false);

        // TODO make static const buffers for these messages
        TRY(send_message(TRY(BitTorrent::Message::interested()), context));
        peer->set_interested_in_peer(true);

        TRY(piece_or_peer_availability_updated(context));
    }
    return true;
}

ErrorOr<void> Comm::insert_piece_in_heap(NonnullRefPtr<Torrent> torrent, u64 piece_index)
{
    auto piece_status = torrent->missing_pieces().get(piece_index).value();
    piece_status->currently_downloading = false;
    torrent->piece_heap().insert(*piece_status);
    return {};
}

}
