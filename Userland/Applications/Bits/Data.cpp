/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Data.h"
#include "Applications/Bits/Data/Command.h"
#include "sys/select.h"
#include <LibCore/Socket.h>
#include <LibCore/System.h>

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
    dbgln("Trying to connect with peer {}", peer);
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

ErrorOr<bool> Data::update_piece_availability(u64 piece_index, SocketContext* socket_context)
{
    auto& torrent = socket_context->torrent;
    auto& peer = socket_context->peer;
    auto is_missing = torrent->missing_pieces().get(piece_index);
    if (!is_missing.has_value()) {
        dbgln("Peer {} has piece {} but we already have it.", peer, piece_index);
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

        auto socket = socket_context->socket;

        // TODO: figure out when is the best time to unchoke the peer.
        TRY(socket->write_value(BigEndian<u32>(1)));
        TRY(socket->write_value((u8)MessageType::Unchoke));
        peer->set_choking_peer(false);

        // TODO make static const buffers for these messages
        TRY(socket->write_value(BigEndian<u32>(1)));
        TRY(socket->write_value((u8)MessageType::Interested));
        peer->set_interested_in_peer(true);

        TRY(piece_or_peer_availability_updated(torrent));
    }
    return true;
}

ErrorOr<void> Data::receive_bitfield(ReadonlyBytes const& bytes, Bits::Data::SocketContext* context)
{
    auto bitfield_data_size = context->torrent->local_bitfield().data_size();
    if (bytes.size() != bitfield_data_size) {
        warnln("Bitfield sent by peer has a size ({}) different than expected({})", bytes.size(), bitfield_data_size);
        return Error::from_string_literal("Bitfield sent by peer has a size different than expected");
    }
    context->peer->set_bitbield(BitField(TRY(ByteBuffer::copy(bytes)), context->torrent->piece_count()));
    dbgln("Set bitfield for peer {}:{}. size: {} data_size: {}", context->peer->address().to_string(), context->peer->port(), context->peer->bitfield().size(), context->peer->bitfield().data_size());

    for (auto& missing_piece : context->torrent->missing_pieces()) {
        if (context->peer->bitfield().get(missing_piece.key))
            TRY(update_piece_availability(missing_piece.key, context));
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
        for (auto& peer : torrent->missing_pieces().get(next_piece_index).value()->havers.keys()) {
            if (!m_peer_to_socket_context.contains(peer)) // filter out peers we failed to connect with, gotta improve this.
                continue;

            dbgln("Peer {} is choking us: {}, is active: {}", peer, peer->is_choking_us(), m_active_peers.contains(peer));
            if (!peer->is_choking_us() && !m_active_peers.contains(peer)) {
                dbgln("Requesting piece {} from peer {}", next_piece_index, peer);
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
            dbgln("Found peer for piece {}, popping the piece from the heap", next_piece_index);
            auto piece_status = torrent->piece_heap().pop_min();
            piece_status->currently_downloading = true;
            VERIFY(piece_status->index_in_torrent == next_piece_index);
        } else {
            dbgln("No more available peer to download piece {}", next_piece_index);
            break;
        }
    }
    return {};
}

ErrorOr<void> Data::read_from_socket(SocketContext* context)
{
    // TODO: cleanup this mess, read everything we can in a buffer at every call first.
    auto& peer = context->peer;
    auto& socket = context->socket;
    //dbgln("Reading from socket with peer {}:{}.", peer->address().to_string(), peer->port());

    // hack:
    if (TRY(socket->pending_bytes()) == 0) {
        dbgln("Socket {}:{} has no pending bytes, reading and writing to it to force receiving an RST", peer->address().to_deprecated_string(), peer->port());
        // remote host probably closed the connection, reading from the socket to force receiving an RST and having the connection being fully closed on our side.
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
                case MessageType::NotInterested:
                    dbgln("Got message type NotInterested");
                    peer->set_peer_is_interested_in_us(false);
                    break;
                case MessageType::Have:
                    dbgln("Got message type Have");
                    TRY(handle_have(context, message_stream));
                    break;
                case MessageType::Bitfield: {
                    dbgln("Got message type Bitfield");
                    TRY(receive_bitfield(message_stream.bytes().slice(1), context));
                    break;
                }
                case MessageType::Request:
                    dbgln("Got message type Request, unsupported");
                    break;
                case MessageType::Piece: {
                    //dbgln("Got message type Piece");
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
                        if (peer->is_choking_us()) {
                            dbgln("Weren't done downloading the blocks for this piece {}, but peer {} is choking us, so we're giving up on it", piece.index.value(), peer);
                            piece.index = {};
                            m_active_peers.remove(peer);
                            TRY(insert_piece_in_heap(context->torrent, index));
                            TRY(piece_or_peer_availability_updated(context->torrent));
                        } else {
                            auto next_block_length = min((size_t)BlockLength, (size_t)piece.length - piece.offset);
                            //dbgln("Sending next request for piece {} at offset {}/{} blocklen: {}", index, piece.offset, piece.length, next_block_length);
                            TRY(socket->write_value(BigEndian<u32>(13)));
                            TRY(socket->write_value((u8)MessageType::Request));
                            TRY(socket->write_value(BigEndian<u32>(index)));
                            TRY(socket->write_value(BigEndian<u32>(piece.offset)));
                            TRY(socket->write_value(BigEndian<u32>(next_block_length)));
                        }
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

ErrorOr<void> Data::insert_piece_in_heap(NonnullRefPtr<Torrent> torrent, u64 piece_index)
{
    auto piece_status = torrent->missing_pieces().get(piece_index).value();
    piece_status->currently_downloading = false;
    torrent->piece_heap().insert(*piece_status);
    return {};
}

ErrorOr<void> Data::handle_have(Bits::Data::SocketContext* context, AK::Stream& stream)
{
    auto piece_index = TRY(stream.read_value<BigEndian<u32>>());
    dbgln("Peer {} has piece {}, setting in peer bitfield, bitfield size: {}", context->peer->address().to_deprecated_string(), piece_index, context->peer->bitfield().size());
    context->peer->bitfield().set(piece_index, true);
    TRY(update_piece_availability(piece_index, context));
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
        auto address = Core::SocketAddress { context->peer->address(), context->peer->port() };
        auto socket_fd = TRY(Core::System::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0));
        auto sockaddr = address.to_sockaddr_in();
        auto connect_err = Core::System::connect(socket_fd, bit_cast<struct sockaddr*>(&sockaddr), sizeof(sockaddr));
        if (connect_err.is_error() && connect_err.error().code() != EINPROGRESS) {
            dbgln("Error connecting to peer {}: {}", address.to_deprecated_string(), connect_err.error());
            continue;
        }

        context->socket_writable_notifier = Core::Notifier::construct(socket_fd, Core::Notifier::Type::Write);
        auto context_ptr = context.leak_ptr();
        auto socket = Core::TCPSocket::adopt_fd(socket_fd).release_value().leak_ptr();
        m_socket_contexts.set(socket, context_ptr);
        m_peer_to_socket_context.set(context_ptr->peer, context_ptr);
        context_ptr->socket = socket;

        context_ptr->socket_writable_notifier->on_activation = [&, socket_fd, context_ptr, socket, address] {
//            dbgln("WRITE notification {}", address.to_deprecated_string());
            int so_error;
            socklen_t len = sizeof(so_error);
            auto ret = getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &so_error, &len);
            if (ret == -1) {
                auto errn = errno;
                dbgln("error calling getsockopt: errno:{} {}", errn, strerror(errn));
                return;
            }
            //dbgln("getsockopt SO_ERROR resut: '{}' ({}) for peer {}", strerror(so_error), so_error, address.to_deprecated_string());

            if (so_error == ECONNREFUSED) {
                dbgln("Connection refused {}", context_ptr->peer);
            } else if (so_error == ETIMEDOUT) {
                dbgln("Connection timed out {}", context_ptr->peer);
            } else if (so_error == ESUCCESS) {
                dbgln("Connection succeeded {}, sending handshake", context_ptr->peer);
                auto handshake = BittorrentHandshake(context_ptr->torrent->meta_info().info_hash(), context_ptr->torrent->local_peer_id());
                socket->write_until_depleted({ &handshake, sizeof(handshake) }).release_value_but_fixme_should_propagate_errors();
                socket->on_ready_to_read = [&, context_ptr] {
                    auto err = read_from_socket(context_ptr);
                    if (err.is_error()) {
                        dbgln("Error reading from socket: {} for peer {}", err.error(), address.to_deprecated_string());
                        return;
                    }
                };
            }
            context_ptr->socket_writable_notifier->close();
        };
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
        auto& havers = torrent->missing_pieces().get(index).value()->havers;
        dbgln("Havers of that piece {} we just downloaded: {}", index, havers.size());
        for (auto& haver_ : havers) {
            auto& haver = haver_.key;
            VERIFY(haver->interesting_pieces().remove(index));
            dbgln("Removed piece {} from interesting pieces of {}", index, haver);
            if (haver->interesting_pieces().is_empty()) {
                dbgln("Peer {} has no more interesting pieces, sending a NotInterested message", haver);
                auto* socket = m_peer_to_socket_context.get(haver).value()->socket;
                TRY(socket->write_value(BigEndian<u32>(1)));
                TRY(socket->write_value((u8)MessageType::NotInterested));
                haver->set_interested_in_peer(false);
            }
        }
        torrent->missing_pieces().remove(index);

        dbgln("We completed piece {}", index);

        for (auto context : m_socket_contexts) {
            if (torrent != context.value->torrent) // TODO: create a hashmap for contexts per torrent
                continue;
            TRY(context.value->socket->write_value(BigEndian<u32>(5)));
            TRY(context.value->socket->write_value((u8)MessageType::Have));
            TRY(context.value->socket->write_value(BigEndian<u32>(index)));
        }
    } else {
        TRY(insert_piece_in_heap(torrent, index));
        dbgln("Piece {} failed hash check", index);
    }
    TRY(piece_or_peer_availability_updated(torrent));
    return {};
}

}
