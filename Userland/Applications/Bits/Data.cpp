/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Data.h"
#include "Applications/Bits/BK/PieceHeap.h"
#include "BitsWidget.h"
#include <LibCore/Socket.h>

namespace Bits {

ErrorOr<BittorrentHandshake> BittorrentHandshake::read_from_stream(Stream& stream)
{
    BittorrentHandshake handshake;
    TRY(stream.read_until_filled(Bytes { &handshake, sizeof(BittorrentHandshake) }));
    return handshake;
}

Data::Data()
{
    m_thread = Threading::Thread::construct([this]() -> intptr_t {
        m_event_loop = make<Core::EventLoop>();
        return m_event_loop->exec();
    },
        "Data thread"sv);
    m_thread->start();
}

ErrorOr<void> Data::read_handshake(Stream& stream, SocketContext* context)
{
    auto handshake = TRY(stream.read_value<BittorrentHandshake>());
    dbgln("Received handshake_message: Protocol: {}, Reserved: {:08b} {:08b} {:08b} {:08b} {:08b} {:08b} {:08b} {:08b}, info_hash: {:20hex-dump}, peer_id: {:20hex-dump}",
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
        dbgln("Peer {}:{} sent a handshake with the wrong info hash.", context->peer->address().to_string(), context->peer->port());
        return Error::from_string_literal("Peer sent a handshake with the wrong info hash.");
    }
    context->got_handshake = true;
    context->incoming_message_length = 0;

    return {};
}

void Data::add_connection(NonnullRefPtr<Peer> peer, NonnullRefPtr<Torrent> torrent)
{
    dbgln("Adding connection with peer {}:{}.", peer->address().to_string(), peer->port());
    Threading::MutexLocker lock(m_sockets_to_create_mutex);
    lock.lock();
    m_sockets_to_create.enqueue(make<SocketContext>(peer, torrent));
    lock.unlock();
    m_event_loop->post_event(*this, make<Core::CustomEvent>(DataEventType::AddConnection));
}

ErrorOr<void> Data::send_local_bitfield(Core::TCPSocket* socket, Bits::Data::SocketContext* context)
{
    dbgln("Sending bitfield_message");
    auto local_bitfield = context->torrent->local_bitfield();
    TRY(socket->write_value(BigEndian<u32>(local_bitfield.data_size() + 1)));
    TRY(socket->write_value((u8)MessageType::Bitfield));
    TRY(socket->write_until_depleted(local_bitfield.bytes()));
    return {};
}

ErrorOr<void> Data::receive_bitfield(Core::TCPSocket* socket, ReadonlyBytes const& bytes, Bits::Data::SocketContext* context)
{
    auto bitfield_data_size = context->torrent->local_bitfield().data_size();
    if (bytes.size() != bitfield_data_size) {
        warnln("Bitfield sent by peer has a size ({}) different than expected({})", bytes.size(), bitfield_data_size);
        return Error::from_string_literal("Bitfield sent by peer has a size different than expected");
    }
    context->peer->set_bitbield(BitField(TRY(ByteBuffer::copy(bytes))));

    auto heap = context->torrent->piece_heap();
    for (auto missing_piece : context->torrent->missing_pieces()) {
        auto piece_av = missing_piece.value;
        if (context->peer->bitfield().get(piece_av->index_in_torrent)) {
            size_t new_index_in_heap;
            if (piece_av->index_in_heap.has_value())
                new_index_in_heap = heap.update_key(piece_av->index_in_heap.value(), heap.at_key(piece_av->index_in_heap.value()) + 1);
            else
                new_index_in_heap = heap.insert(1, piece_av);

            piece_av->index_in_heap.emplace(new_index_in_heap);
        }
    }

    if (!heap.is_empty()) {
        TRY(socket->write_value(BigEndian<u32>(1)));
        TRY(socket->write_value((u8)MessageType::Unchoke));
        context->peer->set_choking_peer(false);

        TRY(socket->write_value(BigEndian<u32>(1)));
        TRY(socket->write_value((u8)MessageType::Interested));
        context->peer->set_interested_in_peer(true);

        auto next_piece = heap.pop_min()->index_in_torrent;
        context->peer->set_incoming_piece_index(next_piece);
        context->peer->set_incoming_piece_offset(0);
        context->peer->incoming_piece().resize(context->torrent->meta_info().piece_length());
        TRY(socket->write_value(BigEndian<u32>(13)));
        TRY(socket->write_value((u8)MessageType::Request));
        TRY(socket->write_value(BigEndian<u32>(next_piece)));
        TRY(socket->write_value(BigEndian<u32>(0)));
        TRY(socket->write_value(BigEndian<u32>(BlockLength)));
    }

    return {};
}

ErrorOr<void> Data::read_from_socket(Core::TCPSocket* socket)
{
    auto maybe_context = m_socket_contexts.get(socket);
    VERIFY(maybe_context.has_value());
    auto& context = maybe_context.value();
    auto& peer = context->peer;
    dbgln("Reading from socket with peer {}:{}.", peer->address().to_string(), peer->port());
    dbgln("socket is eof: {}", socket->is_eof());
    dbgln("socket is open: {}", socket->is_open());
    if (TRY(socket->can_read_without_blocking())) {
        dbgln("Socket is readable");
        if (context->incoming_message_length == 0) {
            dbgln("Incoming message length is 0");
            if (TRY(socket->pending_bytes()) >= 2) {
                dbgln("Socket has at least 2 pending bytes");
                context->incoming_message_length = TRY(socket->read_value<BigEndian<u32>>());
                if (context->incoming_message_length == 0) {
                    dbgln("Received keep-alive");
                    return {};
                } else {
                    context->incoming_message_buffer.clear();
                    context->incoming_message_buffer.ensure_capacity(context->incoming_message_length);
                }
            } else {
                dbgln("Socket has only {} pending bytes", TRY(socket->pending_bytes()));
                return {};
            }
        }

        auto pending_bytes = TRY(socket->pending_bytes());
        dbgln("Socket has {} pending bytes", pending_bytes);
        dbgln("Incoming message length is {}", context->incoming_message_length);
        dbgln("Incoming message buffer size is {}", context->incoming_message_buffer.size());
        auto will_read = min(pending_bytes, context->incoming_message_length - context->incoming_message_buffer.size());
        dbgln("Will read {} bytes", will_read);
        TRY(socket->read_until_filled(TRY(context->incoming_message_buffer.get_bytes_for_writing(will_read))));

        if (context->incoming_message_buffer.size() == context->incoming_message_length) {
            auto message_stream = FixedMemoryStream(context->incoming_message_buffer.bytes());
            if (!context->got_handshake) {
                dbgln("No handshake yet, trying to read and parse it");
                TRY(read_handshake(message_stream, context));
                TRY(send_local_bitfield(socket, context));
            } else {
                auto message_id = TRY(message_stream.read_value<MessageType>());
                context->incoming_message_length = 0;
                switch (message_id) {
                case MessageType::Choke:
                    dbgln("Got message type Choke");
                    peer->set_peer_is_choking_us(true);
                    break;
                case MessageType::Unchoke:
                    dbgln("Got message type Unchoke");
                    peer->set_peer_is_choking_us(false);
                    break;
                case MessageType::Interested:
                    dbgln("Got message type Interested");
                    peer->set_peer_is_interested_in_us(true);
                    break;
                case MessageType::NotInterest:
                    dbgln("Got message type NotInterest");
                    peer->set_peer_is_interested_in_us(false);
                    break;
                case MessageType::Have:
                    dbgln("Got message type Have, unsupported");
                    break;
                case MessageType::Bitfield: {
                    dbgln("Got message type Bitfield");
                    TRY(receive_bitfield(socket, message_stream.bytes().slice(1), context));
                    break;
                }
                case MessageType::Request:
                    dbgln("Got message type Request, unsupported");
                    break;
                case MessageType::Piece: {
                    dbgln("Got message type Piece");
                    auto block_size = TRY(message_stream.size()) - 9;
                    auto index = TRY(message_stream.read_value<BigEndian<u32>>());
                    VERIFY(index == peer->incoming_piece_index());
                    auto begin = TRY(message_stream.read_value<BigEndian<u32>>());
                    peer->incoming_piece().overwrite(begin, message_stream.bytes().slice(9, block_size).data(), block_size);
                    peer->set_incoming_piece_offset(begin + block_size);
                    if (peer->incoming_piece_offset() == (size_t)context->torrent->meta_info().piece_length() - 1) {
                        if (TRY(context->torrent->data_file_map()->validate_hash(index, peer->incoming_piece()))) {
                            TRY(context->torrent->data_file_map()->write_piece(index, peer->incoming_piece()));
                            context->torrent->local_bitfield().set(index, true);
                            // TODO send HAVE message
                            peer->set_incoming_piece_index(peer->incoming_piece_index() + 1);
                            peer->set_incoming_piece_offset(0);
                            dbgln("We downloaded a piece!!!");
                        } else {
                            dbgln("Piece {} failed hash check", index);
                        }
                    } else {
                        auto next_block_length = min((size_t)BlockLength, (size_t)context->torrent->meta_info().piece_length() - peer->incoming_piece_offset());
                        context->peer->incoming_piece().resize(context->torrent->meta_info().piece_length());
                        TRY(socket->write_value(BigEndian<u32>(13)));
                        TRY(socket->write_value((u8)MessageType::Request));
                        TRY(socket->write_value(BigEndian<u32>(peer->incoming_piece_index())));
                        TRY(socket->write_value(BigEndian<u32>(peer->incoming_piece_offset())));
                        TRY(socket->write_value(BigEndian<u32>(next_block_length)));
                    }
                    break;
                }
                case MessageType::Cancel:
                    dbgln("Got message type Cancel, unsupported");
                    break;
                default:
                    dbgln("Got unknown message type: {:02X}", (u8)message_id);
                    break;
                }
            }
        }
    }
    return {};
}

void Data::custom_event(Core::CustomEvent& event)
{
    switch (event.custom_type()) {
    case DataEventType::AddConnection:
        auto err = add_new_connections();
        if (err.is_error())
            dbgln("Error adding new connections: {}", err.error());
        else
            event.accept(); // useful?
        break;
    }
}

ErrorOr<void> Data::add_new_connections()
{
    Vector<NonnullOwnPtr<SocketContext>> to_add;
    Threading::MutexLocker lock(m_sockets_to_create_mutex);
    lock.lock();
    while (!m_sockets_to_create.is_empty())
        to_add.append(m_sockets_to_create.dequeue());
    lock.unlock();

    for (auto& context : to_add) {
        auto peer = context->peer;
        auto torrent = context->torrent;
        auto connect_error = Core::TCPSocket::connect(peer->address().to_deprecated_string(), peer->port());
        if (connect_error.is_error()) {
            dbgln("Failed to connect to peer {}:{}", peer->address().to_string(), peer->port());
        } else {
            auto socket = connect_error.release_value().leak_ptr();
            m_socket_contexts.set(socket, move(context));
            socket->on_ready_to_read = [this, socket] {
                auto err = read_from_socket(socket);
                if (err.is_error())
                    dbgln("Error reading from socket: {}", err.error());
            };
            auto handshake = BittorrentHandshake(torrent->meta_info().info_hash(), torrent->local_peer_id());
            TRY(socket->write_until_depleted({ &handshake, sizeof(handshake) }));
        }
    }
    return {};
}

}
