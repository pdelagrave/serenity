/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Data.h"
#include "Applications/Bits/Data/Command.h"
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

Data::~Data()
{
    m_event_loop->quit(0);
    for (auto socket : m_socket_contexts.keys()) {
        socket->close();
        delete socket;
    }
    m_thread->join().value();
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
    dbgln("Adding connection to peer {}:{}.", peer->address().to_string(), peer->port());
    Threading::MutexLocker lock(m_sockets_to_create_mutex);
    lock.lock();
    m_sockets_to_create.enqueue(make<SocketContext>(nullptr, peer, torrent));
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

ErrorOr<bool> Data::update_piece_availability(u64 piece_index, NonnullRefPtr<Torrent>& torrent)
{
    auto is_missing = torrent->missing_pieces().get(piece_index);
    if (!is_missing.has_value())
        return false;
    auto& piece_av = is_missing.value();
    auto& heap = torrent->piece_heap();
    size_t new_index_in_heap;
    if (piece_av->index_in_heap.has_value())
        new_index_in_heap = heap.update_key(piece_av->index_in_heap.value(), heap.at_key(piece_av->index_in_heap.value()) + 1);
    else
        new_index_in_heap = heap.insert(1, piece_av);

    piece_av->index_in_heap.emplace(new_index_in_heap);
    return true;
}

ErrorOr<void> Data::receive_bitfield(Core::TCPSocket* socket, ReadonlyBytes const& bytes, Bits::Data::SocketContext* context)
{
    auto bitfield_data_size = context->torrent->local_bitfield().data_size();
    if (bytes.size() != bitfield_data_size) {
        warnln("Bitfield sent by peer has a size ({}) different than expected({})", bytes.size(), bitfield_data_size);
        return Error::from_string_literal("Bitfield sent by peer has a size different than expected");
    }
    context->peer->set_bitbield(BitField(TRY(ByteBuffer::copy(bytes))));
    dbgln("Set bitfield for peer {}:{}. size: {} data_size: {}", context->peer->address().to_string(), context->peer->port(), context->peer->bitfield().size(), context->peer->bitfield().data_size());

    bool interested = false;
    for (auto& missing_piece : context->torrent->missing_pieces()) {
        if (context->peer->bitfield().get(missing_piece.key))
            interested |= TRY(update_piece_availability(missing_piece.key, context->torrent));
    }

    if (interested) {
        // TODO: figure out when is the best time to unchoke the peer.
        TRY(socket->write_value(BigEndian<u32>(1)));
        TRY(socket->write_value((u8)MessageType::Unchoke));
        context->peer->set_choking_peer(false);

        // TODO make static const buffers for these messages
        TRY(socket->write_value(BigEndian<u32>(1)));
        TRY(socket->write_value((u8)MessageType::Interested));
        context->peer->set_interested_in_peer(true);

        TRY(piece_or_peer_availability_updated(context->torrent));
    }

    return {};
}

ErrorOr<void> Data::piece_or_peer_availability_updated(NonnullRefPtr<Torrent>& torrent)
{
    size_t max_to_add = max_active_peers - m_active_peers.size();
    dbgln("We can add {} peers to download stuff ({}/{})", max_to_add, m_active_peers.size(), max_active_peers);
    for (size_t i = 0; i < max_to_add; i++) {
        dbgln("Trying to add a {}th peer to download stuff", i);
        if (torrent->piece_heap().is_empty())
            return {};

        auto next_piece_index = torrent->piece_heap().peek_min()->index_in_torrent;
        dbgln("Picked next piece for download {}", next_piece_index);
        // TODO improve how we select the peer. Choking algo, bandwidth, etc
        bool found_peer = false;
        for (auto& peer : torrent->peers()) {
            if (!m_peer_to_socket_context.contains(peer)) // filter out peers we failed to connect with, gotta improve this.
                continue;

            dbgln("Peer {} is choking us: {}, has piece: {}, is active: {}", peer->address().to_string(), peer->is_choking_us(), peer->bitfield().get(next_piece_index), m_active_peers.contains(peer));
            if (!peer->is_choking_us() && peer->bitfield().get(next_piece_index) && !m_active_peers.contains(peer)) {
                dbgln("Requesting piece {} from peer {}", next_piece_index, peer->address().to_string());
                m_active_peers.set(peer, nullptr);
                auto socket = m_peer_to_socket_context.get(peer).value()->socket;
                TRY(socket->write_value(BigEndian<u32>(13)));
                TRY(socket->write_value((u8)MessageType::Request));
                TRY(socket->write_value(BigEndian<u32>(next_piece_index)));
                TRY(socket->write_value(BigEndian<u32>(0)));
                TRY(socket->write_value(BigEndian<u32>(min(BlockLength, torrent->piece_length(next_piece_index)))));
                found_peer = true;
                break;
            }
        }
        if (found_peer) {
            VERIFY(torrent->piece_heap().pop_min()->index_in_torrent == next_piece_index);
        } else {
            dbgln("No more available peer to download piece {}", next_piece_index);
            break;
        }
    }
    return {};
}

ErrorOr<void> Data::read_from_socket(Core::TCPSocket* socket)
{
    // TODO: cleanup this mess, read everything we can in a buffer at every call first.
    auto maybe_context = m_socket_contexts.get(socket);
    VERIFY(maybe_context.has_value());
    auto& context = maybe_context.value();
    auto& peer = context->peer;
    //    dbgln("Reading from socket with peer {}:{}.", peer->address().to_string(), peer->port());
    //    dbgln("socket is eof: {}", socket->is_eof());
    //    dbgln("socket is open: {}", socket->is_open());

    // hack:
    if (TRY(socket->pending_bytes()) == 0) {
        dbgln("Socket has no pending bytes, reading and writing to it to force receiving a RST");
        // remote host probably closed the connection, reading from the socket to force receiving a RST and having the connection being fully closed on our side.
        TRY(socket->read_some(ByteBuffer::create_uninitialized(1).release_value().bytes()));
        TRY(socket->write_value(BigEndian<u32>(0)));
        return {};
    }
    if (TRY(socket->can_read_without_blocking())) {
        if (context->incoming_message_length == 0) {
            //            dbgln("Incoming message length is 0");
            if (TRY(socket->pending_bytes()) >= 2) {
                //                dbgln("Socket has at least 2 pending bytes");
                context->incoming_message_length = TRY(socket->read_value<BigEndian<u32>>());
                if (context->incoming_message_length == 0) {
                    dbgln("Received keep-alive");
                    return {};
                } else {
                    context->incoming_message_buffer.clear();
                    context->incoming_message_buffer.ensure_capacity(context->incoming_message_length);
                }
            } else {
                //                dbgln("Socket has only {} pending bytes", TRY(socket->pending_bytes()));
                return {};
            }
        }

        auto pending_bytes = TRY(socket->pending_bytes());
        //        dbgln("Socket has {} pending bytes", pending_bytes);
        //        dbgln("Incoming message length is {}", context->incoming_message_length);
        //        dbgln("Incoming message buffer size is {}", context->incoming_message_buffer.size());
        auto will_read = min(pending_bytes, context->incoming_message_length - context->incoming_message_buffer.size());
        //        dbgln("Will read {} bytes", will_read);
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
                    TRY(piece_or_peer_availability_updated(context->torrent));
                    break;
                case MessageType::Unchoke:
                    dbgln("Got message type Unchoke");
                    peer->set_peer_is_choking_us(false);
                    TRY(piece_or_peer_availability_updated(context->torrent));
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
                    dbgln("Got message type Have");
                    TRY(handle_have(context, message_stream));
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
                        Core::EventLoop::current().post_event(*this, make<PieceDownloadedCommand>(index, piece.data.bytes(), context->torrent));
                        piece.index = {};
                        m_active_peers.remove(peer);
                    } else {
                        auto next_block_length = min((size_t)BlockLength, (size_t)piece.length - piece.offset);
                        dbgln("Sending next request for piece {} at offset {}/{} blocklen: {}", index, piece.offset, piece.length, next_block_length);
                        TRY(socket->write_value(BigEndian<u32>(13)));
                        TRY(socket->write_value((u8)MessageType::Request));
                        TRY(socket->write_value(BigEndian<u32>(index)));
                        TRY(socket->write_value(BigEndian<u32>(piece.offset)));
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

ErrorOr<void> Data::handle_have(Bits::Data::SocketContext* context, AK::Stream& stream)
{
    auto piece_index = TRY(stream.read_value<BigEndian<u32>>());
    context->peer->bitfield().set(piece_index, true);
    if (TRY(update_piece_availability(piece_index, context->torrent))) {
        if (!context->peer->is_interested_in_peer()) {
            TRY(context->socket->write_value(BigEndian<u32>(1)));
            TRY(context->socket->write_value((u8)MessageType::Interested));
            context->peer->set_interested_in_peer(true);
        }
        TRY(piece_or_peer_availability_updated(context->torrent));
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
            context->socket = socket;
            SocketContext* context_ptr = context.leak_ptr();
            m_socket_contexts.set(socket, context_ptr);
            m_peer_to_socket_context.set(peer, context_ptr);
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

void Data::event(Core::Event& event)
{
    auto err = [&]() -> ErrorOr<void> {
        switch (static_cast<Command::Type>(event.type())) {
        case Command::Type::PieceDownloaded:
            return handle_piece_downloaded(static_cast<PieceDownloadedCommand const&>(event));
        default:
            Object::event(event);
            break;
        }
        return {};
    }();
    if (err.is_error())
        dbgln("Error handling event: {}", err.error());
}

ErrorOr<void> Data::handle_piece_downloaded(Bits::PieceDownloadedCommand const& command)
{
    auto torrent = command.torrent();
    auto index = command.index();
    auto data = command.data();
    if (TRY(torrent->data_file_map()->validate_hash(index, data))) {
        TRY(torrent->data_file_map()->write_piece(index, data));

        torrent->local_bitfield().set(index, true);
        torrent->missing_pieces().remove(index);
        dbgln("We completed piece {}", index);

        for (auto context : m_socket_contexts) {
            if (torrent != context.value->torrent) // TODO: create a hashmap for contexts per torrent
                continue;
            TRY(context.value->socket->write_value(BigEndian<u32>(5)));
            TRY(context.value->socket->write_value((u8)MessageType::Have));
            TRY(context.value->socket->write_value(BigEndian<u32>(index)));
        }
        // TODO once we have downloaded a new piece, send NOT INTERESTED to peers that we no longer need pieces from
        TRY(piece_or_peer_availability_updated(torrent));
    } else {
        // TODO reinsert piece in heap
        dbgln("Piece {} failed hash check", index);
    }
    return {};
}

}
