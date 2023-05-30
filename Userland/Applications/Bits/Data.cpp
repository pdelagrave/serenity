/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Data.h"
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

ErrorOr<void> Data::read_handshake(Core::TCPSocket* socket, SocketContext& context)
{
    auto handshake = TRY(socket->read_value<BittorrentHandshake>());
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

    if (context.torrent->meta_info().info_hash() != Bytes { handshake.info_hash, 20 }) {
        dbgln("Peer {}:{} sent a handshake with the wrong info hash.", context.peer->address().to_string(), context.peer->port());
        socket->close();
        return Error::from_string_literal("Peer sent a handshake with the wrong info hash.");
    }
    context.got_handshake = true;
    context.incoming_message_length = 0;

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

ErrorOr<void> Data::read_from_socket(Core::TCPSocket* socket)
{
    dbgln("Reading from socket");
    auto maybe_context = m_socket_contexts.get(socket);
    VERIFY(maybe_context.has_value());
    auto& context = maybe_context.value();
    auto& peer = context->peer;

    if (TRY(socket->can_read_without_blocking())) {
        dbgln("Socket is readable");
        if (context->incoming_message_length == 0) {
            dbgln("Incoming message length is 0");
            if (TRY(socket->pending_bytes()) >= 2) {
                dbgln("Socket has at least 2 pending bytes");
                context->incoming_message_length = TRY(socket->read_value<BigEndian<u32>>());
            } else {
                return {};
            }
        }
        dbgln("{}:{} Incoming message length is {}", peer->address().to_string(), peer->port(), context->incoming_message_length);
        dbgln("{}:{} Socket has {} pending bytes", peer->address().to_string(), peer->port(), TRY(socket->pending_bytes()));
        if (TRY(socket->pending_bytes()) >= context->incoming_message_length) {
            dbgln("Socket has at least {} pending bytes", context->incoming_message_length);
            if (!context->got_handshake) {
                dbgln("No handshake yet, trying to read and parse it");
                read_handshake(socket, *context).release_value();
            } else {
                auto message_id = TRY(socket->read_value<MessageType>());
                ByteBuffer buffer = TRY(ByteBuffer::create_uninitialized(context->incoming_message_length - 1));
                TRY(socket->read_until_filled(buffer));
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
                    dbgln("Got message type Interested, unsupported");
                    peer->set_peer_is_interested_in_us(true);
                    break;
                case MessageType::NotInterest:
                    dbgln("Got message type NotInterest, unsupported");
                    peer->set_peer_is_interested_in_us(false);
                    break;
                case MessageType::Have:
                    dbgln("Got message type Have, unsupported");
                    break;
                case MessageType::Bitfield: {
                    dbgln("Got message type Bitfield");

                    auto bitfield_data_size = context->torrent->local_bitfield().data_size();
                    if (buffer.size() != bitfield_data_size) {
                        warnln("Bitfield sent by peer has a size ({}) different than expected({})", buffer.size(), bitfield_data_size);
                        return Error::from_string_literal("Bitfield sent by peer has a size different than expected");
                    }
                    peer->set_bitbield(BitField(buffer));
                    break;
                }
                case MessageType::Request:
                    dbgln("Got message type Request, unsupported");
                    break;
                case MessageType::Piece:
                    dbgln("Got message type Piece, unsupported");
                    break;
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
