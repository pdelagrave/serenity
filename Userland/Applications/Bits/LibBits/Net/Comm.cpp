/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Comm.h"
#include "BitTorrentMessage.h"
#include "PeerContext.h"
#include <LibCore/System.h>

namespace Bits {

Comm::Comm()
    : m_server(Core::TCPServer::try_create(this).release_value())
{
    m_thread = Threading::Thread::construct([this]() -> intptr_t {
        m_event_loop = make<Core::EventLoop>();

        auto err = m_server->set_blocking(false);
        if (err.is_error()) {
            dbgln("Failed to set server to blocking mode: {}", err.error());
            return 1;
        }

        m_server->on_ready_to_accept = [&] {
            auto err = on_ready_to_accept();
            if (err.is_error())
                dbgln("Failed to accept connection: {}", err.error());
        };
        err = m_server->listen(IPv4Address::from_string("0.0.0.0"sv).release_value(), 27007);
        if (err.is_error()) {
            dbgln("Failed to listen: {}", err.error());
            return 1;
        }

        gettimeofday(&m_last_speed_measurement, nullptr);
        start_timer(1000);
        return m_event_loop->exec();
    },
        "Comm thread"sv);

    m_thread->start();
}

void Comm::activate_torrent(NonnullRefPtr<TorrentContext> torrent)
{
    m_event_loop->deferred_invoke([this, torrent = move(torrent)] {
        for (u64 i = 0; i < torrent->piece_count; i++) {
            if (!torrent->local_bitfield.get(i))
                torrent->missing_pieces.set(i, make_ref_counted<PieceStatus>(i));
        }
        m_torrent_contexts.set(torrent->info_hash, move(torrent));
    });
}

void Comm::deactivate_torrent(InfoHash info_hash)
{
    m_event_loop->deferred_invoke([this, info_hash] {
        // TODO make sure add_peers can't and won't be called during deactivation
        auto torrent = m_torrent_contexts.get(info_hash).value();
        for (auto const& peer : torrent->all_peers) {
            if (peer->connected)
                set_peer_errored(peer, false);
        }
        m_torrent_contexts.remove(info_hash);
    });
}

void Comm::add_peers(InfoHash info_hash, Vector<Core::SocketAddress> peers)
{
    m_event_loop->deferred_invoke([this, info_hash, peers] {
        auto torrent = m_torrent_contexts.get(info_hash).value();
        for (auto const& peer_address : peers) {
            // TODO algo for dupes, deleted (tracker announce doesn't return one anymore and we're downloading/uploading from it?)
            auto peer = make_ref_counted<PeerContext>(*torrent, peer_address);
            // assuming peers are added only once
            torrent->all_peers.set(move(peer));
        }
        connect_more_peers(*torrent);
    });
}

HashMap<InfoHash, TorrentView> Comm::state_snapshot()
{
    Threading::MutexLocker locker(m_state_snapshot_lock);
    return m_state_snapshot;
}

void Comm::timer_event(Core::TimerEvent&)
{
    //TODO clean this up, put each in their own method, also make it so that we can have different intervals

    // Transfer speed measurement
    timeval current_time;
    timeval time_diff;
    gettimeofday(&current_time, nullptr);
    timersub(&current_time, &m_last_speed_measurement, &time_diff);
    auto time_diff_ms = time_diff.tv_sec * 1000 + time_diff.tv_usec / 1000;
    for (auto const& torrent : m_torrent_contexts) {
        u64 upload_speed = 0;
        u64 download_speed = 0;
        for (auto const& peer : torrent.value->connected_peers) {
            auto& c = peer->connection;
            c->download_speed = (c->bytes_downloaded_since_last_speed_measurement / time_diff_ms) * 1000;
            c->bytes_downloaded_since_last_speed_measurement = 0;
            download_speed += c->download_speed;

            c->upload_speed = (c->bytes_uploaded_since_last_speed_measurement / time_diff_ms) * 1000;
            c->bytes_uploaded_since_last_speed_measurement = 0;
            upload_speed += c->upload_speed;
        }
        torrent.value->upload_speed = upload_speed;
        torrent.value->download_speed = download_speed;
    }
    m_last_speed_measurement = current_time;

    // Peers keepalive
    auto keepalive_timeout = Duration::from_seconds(120);
    auto now = Core::DateTime::now();
    for (auto const& torrent : m_torrent_contexts) {
        for (auto const& peer : torrent.value->connected_peers) {
            if (now.timestamp() - peer->connection->last_message_received_at.timestamp() > keepalive_timeout.to_milliseconds() + 10000) {
                dbgln("Peer timed out");
                set_peer_errored(peer);
                continue;
            }

            if (now.timestamp() - peer->connection->last_message_sent_at.timestamp() > keepalive_timeout.to_milliseconds() - 10000) {
                dbgln("Sending keepalive");
                send_message(make<KeepAliveMessage>(), peer);
            }
        }
    }

    // State snapshot
    HashMap<InfoHash, TorrentView> new_snapshot;
    for (auto const& [info_hash, torrent] : m_torrent_contexts) {
        Vector<PeerView> pviews;
        pviews.ensure_capacity(torrent->all_peers.size());
        for (auto const& peer : torrent->all_peers) {
            pviews.append(PeerView(
                peer->id,
                peer->address.ipv4_address().to_deprecated_string(),
                peer->address.port(),
                peer->bitfield.progress(),
                peer->connected ? peer->connection->download_speed : 0,
                peer->connected ? peer->connection->upload_speed : 0,
                peer->we_are_choking_peer,
                peer->peer_is_choking_us,
                peer->we_are_interested_in_peer,
                peer->peer_is_interested_in_us,
                peer->connected ? peer->connection->role : PeerRole::Server
                ));
        }
        new_snapshot.set(info_hash, TorrentView(
                                        info_hash,
                                        "displayname", // FIXME bad abstraction
                                        torrent->total_length,
                                        TorrentState::STARTED, // FIXME bad abstraction
                                        torrent->local_bitfield.progress(),
                                        torrent->download_speed,
                                        torrent->upload_speed,
                                        "savepath", // FIXME bad abstraction
                                        pviews
                                        ));
    }

    m_state_snapshot_lock.lock();
    m_state_snapshot = new_snapshot;
    m_state_snapshot_lock.unlock();
}

ErrorOr<void> Comm::piece_downloaded(u64 index, ReadonlyBytes data, NonnullRefPtr<PeerContext> peer)
{
    auto torrent = peer->torrent_context;
    if (TRY(torrent->data_file_map->validate_hash(index, data))) {
        TRY(torrent->data_file_map->write_piece(index, data));

        torrent->local_bitfield.set(index, true);
        auto& havers = torrent->missing_pieces.get(index).value()->havers;
        dbglnc("Havers of that piece {} we just downloaded: {}", index, havers.size());
        for (auto& haver : havers) {
            VERIFY(haver->interesting_pieces.remove(index));
            dbglnc("Removed piece {} from interesting pieces of {}", index, haver);
            if (haver->interesting_pieces.is_empty()) {
                dbglnc("Peer {} has no more interesting pieces, sending a NotInterested message", haver);
                send_message(make<NotInterestedMessage>(), haver);
                haver->we_are_interested_in_peer = false;
            }
        }
        torrent->missing_pieces.remove(index);

        dbglnc("We completed piece {}", index);

        for (auto const& connected_peer : torrent->connected_peers) {
            m_peer_context_stack.append(connected_peer);
            send_message(make<HaveMessage>(index), connected_peer);
            m_peer_context_stack.remove(m_peer_context_stack.size() - 1);
        }

        if (torrent->local_bitfield.progress() == 100) {
            dbglnc("Torrent fully downloaded.");
            VERIFY(torrent->piece_heap.is_empty());
            VERIFY(torrent->missing_pieces.is_empty());

            for (auto const& connected_peer : torrent->connected_peers) {
                if (connected_peer->bitfield.progress() == 100)
                    set_peer_errored(connected_peer, false);
            }
            // TODO: Have the engine monitor for status and do the deactivation
            // deactivate_torrent(torrent->info_hash);
            return {};
        }
    } else {
        insert_piece_in_heap(torrent, index);
        dbglnc("Piece {} failed hash check", index);
    }
    TRY(piece_or_peer_availability_updated(torrent));
    return {};
}

ErrorOr<void> Comm::piece_or_peer_availability_updated(NonnullRefPtr<TorrentContext> torrent)
{
    size_t available_slots = 0;
    for (auto const& peer : torrent->connected_peers)
        available_slots += !peer->active;

    dbglnc("We have {} inactive peers out of {} connected peers.", available_slots, torrent->connected_peers.size());
    for (size_t i = 0; i < available_slots; i++) {
        dbglnc("Trying to start a piece download on a {}th peer", i);
        if (torrent->piece_heap.is_empty())
            return {};

        // TODO find out the rarest available piece, because the rarest piece might not be available right now.
        auto next_piece_index = torrent->piece_heap.peek_min()->index_in_torrent;
        dbglnc("Picked next piece for download {}", next_piece_index);
        // TODO improve how we select the peer. Choking algo, bandwidth, etc
        bool found_peer = false;
        for (auto& haver : torrent->missing_pieces.get(next_piece_index).value()->havers) {
            VERIFY(haver->connected);

            dbglnc("Peer {} is choking us: {}, is active: {}", haver, haver->peer_is_choking_us, haver->active);
            if (!haver->peer_is_choking_us && !haver->active) {
                m_peer_context_stack.append(haver);
                dbglnc("Requesting piece {} from peer {}", next_piece_index, haver);
                haver->active = true;
                u32 block_length = min(BlockLength, torrent->piece_length(next_piece_index));
                send_message(make<RequestMessage>(next_piece_index, 0, block_length), *haver);
                m_peer_context_stack.remove(m_peer_context_stack.size() - 1);
                found_peer = true;
                break;
            }
        }
        if (found_peer) {
            dbglnc("Found peer for piece {}, popping the piece from the heap", next_piece_index);
            auto piece_status = torrent->piece_heap.pop_min();
            piece_status->currently_downloading = true;
            VERIFY(piece_status->index_in_torrent == next_piece_index);
        } else {
            dbglnc("No more available peer to download piece {}", next_piece_index);
            break;
        }
    }
    return {};
}

ErrorOr<void> Comm::peer_has_piece(u64 piece_index, NonnullRefPtr<PeerContext> peer)
{
    auto& torrent = peer->torrent_context;
    auto piece_status = torrent->missing_pieces.get(piece_index).value();
    piece_status->havers.set(peer);

    // A piece being downloaded won't be in the heap
    if (!piece_status->currently_downloading) {
        if (piece_status->index_in_heap.has_value()) {
            // The piece is missing and other peers have it.
            torrent->piece_heap.update(*piece_status);
        } else {
            // The piece is missing and this is the first peer we learn of that has it.
            torrent->piece_heap.insert(*piece_status);
        }
    } else {
        VERIFY(!piece_status->index_in_heap.has_value());
    }

    peer->interesting_pieces.set(piece_index);

    return {};
}

void Comm::insert_piece_in_heap(NonnullRefPtr<TorrentContext> torrent, u64 piece_index)
{
    auto piece_status = torrent->missing_pieces.get(piece_index).value();
    piece_status->currently_downloading = false;
    torrent->piece_heap.insert(*piece_status);
}

ErrorOr<void> Comm::parse_input_message(SeekableStream& stream, NonnullRefPtr<PeerContext> peer)
{
    using MessageType = Bits::Message::Type;
    auto message_type = TRY(stream.read_value<MessageType>());
    TRY(stream.seek(0, AK::SeekMode::SetPosition));

    dbglnc("Got message type {}", Bits::Message::to_string(message_type));

    peer->connection->last_message_received_at = Core::DateTime::now();

    peer->connection->incoming_message_length = 0;

    switch (message_type) {
    case MessageType::Choke:
        peer->peer_is_choking_us = true;
        TRY(piece_or_peer_availability_updated(peer->torrent_context));
        break;
    case MessageType::Unchoke:
        peer->peer_is_choking_us = false;
        TRY(piece_or_peer_availability_updated(peer->torrent_context));
        break;
    case MessageType::Interested:
        TRY(handle_interested(peer));
        break;
    case MessageType::NotInterested:
        peer->peer_is_interested_in_us = false;
        break;
    case MessageType::Have:
        TRY(handle_have(make<HaveMessage>(stream), peer));
        break;
    case MessageType::Bitfield:
        TRY(handle_bitfield(make<BitFieldMessage>(stream), peer));
        break;
    case MessageType::Request:
        TRY(handle_request(make<RequestMessage>(stream), peer));
        break;
    case MessageType::Piece:
        TRY(handle_piece(make<PieceMessage>(stream), peer));
        break;
    case MessageType::Cancel:
        dbglnc("ERROR: message type Cancel is unsupported");
        break;
    default:
        dbglnc("ERROR: Got unsupported message type: {:02X}: {}", (u8)message_type, Message::to_string(message_type));
        break;
    }
    return {};
}

ErrorOr<void> Comm::handle_bitfield(NonnullOwnPtr<BitFieldMessage> bitfield, NonnullRefPtr<PeerContext> peer)
{
    peer->bitfield = bitfield->bitfield;
    dbglnc("Set bitfield for peer. size: {} data_size: {}", peer->bitfield.size(), peer->bitfield.data_size());

    bool interesting = false;
    auto torrent = peer->torrent_context;
    for (auto& missing_piece : torrent->missing_pieces.keys()) {
        if (peer->bitfield.get(missing_piece)) {
            interesting = true;
            TRY(peer_has_piece(missing_piece, peer));
        }
    }

    VERIFY(!peer->we_are_interested_in_peer);

    if (interesting) {
        // TODO we need a (un)choking algo
        send_message(make<UnchokeMessage>(), peer);
        peer->we_are_choking_peer = false;

        send_message(make<InterestedMessage>(), peer);
        peer->we_are_interested_in_peer = true;

        TRY(piece_or_peer_availability_updated(torrent));
    } else {
        if (get_available_peers_count(torrent) > 0) {
            // TODO: stay connected if the peer is interested by us.
            dbglnc("Peer has no interesting pieces, disconnecting");
            set_peer_errored(peer); // TODO: set error type so we can connect to it again later if we need to
        } else {
            dbglnc("Peer has no interesting pieces, but we have no other peers to connect to. Staying connected in the hope that it will get some interesting pieces.");
        }
    }

    return {};
}

ErrorOr<void> Comm::handle_have(NonnullOwnPtr<HaveMessage> have_message, NonnullRefPtr<PeerContext> peer)
{
    auto piece_index = have_message->piece_index;
    dbglnc("Peer has piece {}, setting in peer bitfield, bitfield size: {}", piece_index, peer->bitfield.size());
    peer->bitfield.set(piece_index, true);

    if (peer->torrent_context->missing_pieces.contains(piece_index)) {
        TRY(peer_has_piece(piece_index, peer));
        if (!peer->we_are_interested_in_peer) {
            send_message(make<UnchokeMessage>(), peer);
            peer->we_are_choking_peer = false;

            send_message(make<InterestedMessage>(), peer);
            peer->we_are_interested_in_peer = true;
        }
        TRY(piece_or_peer_availability_updated(peer->torrent_context));
    } else {
        if (peer->bitfield.progress() == 100 && peer->torrent_context->local_bitfield.progress() == 100) {
            dbglnc("Peer and us have all pieces, disconnecting");
            set_peer_errored(peer, false);
        }
    }

    return {};
}

ErrorOr<void> Comm::handle_interested(NonnullRefPtr<Bits::PeerContext> peer)
{
    peer->peer_is_interested_in_us = true;
    peer->we_are_choking_peer = false;
    send_message(make<UnchokeMessage>(), peer);
    return {};
}

ErrorOr<void> Comm::handle_piece(NonnullOwnPtr<PieceMessage> piece_message, NonnullRefPtr<PeerContext> peer)
{
    auto torrent = peer->torrent_context;
    auto block_size = piece_message->block.size();
    auto index = piece_message->piece_index;
    auto begin = piece_message->begin_offset;

    auto& piece = peer->incoming_piece;
    if (piece.index.has_value()) {
        VERIFY(index == piece.index);
        VERIFY(begin == piece.offset);
    } else {
        VERIFY(begin == 0);
        piece.index = index;
        piece.offset = 0;
        piece.length = torrent->piece_length(index);
        piece.data.resize(piece.length);
    }

    piece.data.overwrite(begin, piece_message->block.bytes().data(), block_size);
    piece.offset = begin + block_size;
    if (piece.offset == (size_t)piece.length) {
        m_event_loop->deferred_invoke([this, index, peer, bytes = piece.data.bytes()] {
            m_peer_context_stack.append(peer);
            auto err = piece_downloaded(index, bytes, peer);
            m_peer_context_stack.clear();
            if (err.is_error()) {
                dbgln("Failed to handle a downloaded piece: {}", err.error());
                // TODO we should shutdown the app at this point and maybe just not use ErrorOr and release the values
            }
        });
        piece.index = {};
        peer->active = false;
    } else {
        if (peer->peer_is_choking_us) {
            dbglnc("Weren't done downloading the blocks for this piece {}, but peer is choking us, so we're giving up on it", index);
            piece.index = {};
            peer->active = false;
            insert_piece_in_heap(torrent, index);
            TRY(piece_or_peer_availability_updated(torrent));
        } else {
            auto next_block_length = min((size_t)BlockLength, (size_t)piece.length - piece.offset);
            send_message(make<RequestMessage>(index, piece.offset, next_block_length), peer);
        }
    }
    return {};
}

ErrorOr<void> Comm::handle_request(NonnullOwnPtr<RequestMessage> request, NonnullRefPtr<PeerContext> peer)
{
    // TODO: validate request parameters, disconnect peer if they're invalid.
    auto torrent = peer->torrent_context;
    auto piece = TRY(ByteBuffer::create_uninitialized(torrent->piece_length(request->piece_index)));
    TRY(torrent->data_file_map->read_piece(request->piece_index, piece));

    send_message(make<PieceMessage>(request->piece_index, request->piece_offset, TRY(piece.slice(request->piece_offset, request->block_length))), peer);
    return {};
}

ErrorOr<void> Comm::read_from_socket(NonnullRefPtr<PeerConnection> connection)
{
    auto& socket = connection->socket;

    for (;;) {
        auto nread_or_error = connection->input_message_buffer.fill_from_stream(*socket);
        if (socket->is_eof()) {
            dbglnc("Peer disconnected");
            for (auto& [_, torrent] : m_torrent_contexts) {
                for (auto& peer : torrent->all_peers) {
                    if (peer->connection == connection) {
                        set_peer_errored(peer);
                        return {};
                    }
                }
            }
            dbglnc("Connection wasn't associated with any peercontext, so we weren't connected");
            auto maybe_peer = m_connecting_peers.get(connection);
            if (maybe_peer.has_value()) {
                auto peer = maybe_peer.value();
                dbglnc("Connection was initiated by us and we were trying to connect to a peer ({}) but it failed, erroring this peer now.", peer);
                peer->connection = connection;
                set_peer_errored(*peer);
                m_connecting_peers.remove(connection);
            } else {
                dbglnc("Connection wasn't initiated by us, so we didn't have a peercontext for it, just closing it and removing from m_accepted_connections");
                connection->socket_writable_notifier->close();
                connection->socket->close();
                VERIFY(m_accepted_connections.remove(connection));
            }
            return {};
        }
        if (nread_or_error.is_error()) {
            auto code = nread_or_error.error().code();
            if (code == EINTR) {
                continue;
            } else if (code == EAGAIN) {
                break;
            } else {
                dbglnc("Error reading from socket: err: {}  code: {}  codestr: {}", nread_or_error.error(), code, strerror(code));
                return Error::from_string_literal("Error reading from socket");
            }
        }
        connection->bytes_downloaded_since_last_speed_measurement += nread_or_error.value();
    }

    while (connection->input_message_buffer.used_space() >= connection->incoming_message_length) {
        if (connection->incoming_message_length > 0) {
            auto buffer = TRY(ByteBuffer::create_uninitialized(connection->incoming_message_length));
            VERIFY(connection->input_message_buffer.read(buffer.bytes()).size() == connection->incoming_message_length);
            auto message_byte_stream = FixedMemoryStream(buffer.bytes());
            if (!connection->handshake_received) {
                dbglnc("No handshake yet, trying to read and parse it");
                auto handshake = TRY(HandshakeMessage::try_create(message_byte_stream));
                dbglnc("Received handshake: {}", handshake->to_string());

                auto remote_info_hash = InfoHash({handshake->info_hash, 20});
                if (!m_torrent_contexts.contains(remote_info_hash)) {
                    dbglnc("Peer sent a handshake with an unknown info hash.");
                    // TODO set_peer_errored
                    return Error::from_string_literal("Peer sent a handshake with an unknown info hash.");
                }

                auto tcontext = m_torrent_contexts.get(remote_info_hash).value();

                if (Bytes {handshake->peer_id, 20} ==  tcontext->local_peer_id) {
                    dbglnc("Trying to connect to ourselves, disconnecting.");
                    m_connecting_peers.remove(connection);
                    m_accepted_connections.remove(connection);
                    close_connection(connection);
                    return {};
                }


                RefPtr<PeerContext> maybe_peer;
                // If we initiated the connection
                if (m_connecting_peers.contains(connection)) {
                    VERIFY(connection->handshake_sent);
                    dbglnc("We initiated the connection and got back the handshake from peer, sending our bitfield.");
                    maybe_peer = m_connecting_peers.get(connection).value();
                    if (maybe_peer->torrent_context->info_hash != remote_info_hash) {
                        dbglnc("Peer sent a handshake with the wrong info hash.");
                        set_peer_errored(maybe_peer.release_nonnull());
                        return Error::from_string_literal("Peer sent a handshake with the wrong info hash.");
                    }
                    m_connecting_peers.remove(connection);
                } else {
                    VERIFY(!connection->handshake_sent);
                    VERIFY(m_accepted_connections.remove(connection));
                    dbglnc("We accepted the connection with our listening server and got the handshake from peer, sending ours + our bitfield.");

                    // TODO check if we refuse the connection based on limits
                    connection->socket_writable_notifier->set_enabled(true);
                    if (send_handshake(tcontext->info_hash, tcontext->local_peer_id, connection).is_error()) {
                        connection->socket_writable_notifier->close();
                        connection->socket->close();
                        return {};
                    }
                    maybe_peer = make_ref_counted<PeerContext>(*tcontext, connection->socket->address());
                    tcontext->all_peers.set(*maybe_peer);
                }
                NonnullRefPtr<PeerContext> peer = maybe_peer.release_nonnull();
                peer->connection = connection;
                connection->handshake_received = true;
                send_message(make<BitFieldMessage>(BitField(tcontext->local_bitfield)), peer);

                // TODO rename connection to session?
                dbglnc("Connection fully established");
                peer->connected = true;
                peer->torrent_context->connected_peers.set(peer);
            } else {
                // TODO use a hashmap
                for (auto& [_, torrent] : m_torrent_contexts) {
                    for (auto& peer : torrent->all_peers) {
                        if (peer->connection == connection) {
                            TRY(parse_input_message(message_byte_stream, peer));
                            goto d;
                        }
                    }
                }
            d:;
            }
            connection->incoming_message_length = 0;
        } else if (connection->input_message_buffer.used_space() >= sizeof(connection->incoming_message_length)) {
            connection->input_message_buffer.read({ &connection->incoming_message_length, sizeof(connection->incoming_message_length) });
            if (connection->incoming_message_length == 0) {
                dbglnc("Received keep-alive");
                connection->last_message_received_at = Core::DateTime::now();
            }
        } else {
            // Not enough bytes to read the length of the next message
            return {};
        }
    }
    return {};
}

void Comm::send_message(NonnullOwnPtr<Message> message, NonnullRefPtr<PeerContext> peer)
{
    NonnullRefPtr<PeerConnection> connection = *peer->connection;
    auto size = BigEndian<u32>(message->size());
    dbglnc("Sending message [{}b] {}", size, *message);
    size_t total_size = message->size() + sizeof(u32); // message size + message payload
    if (connection->output_message_buffer.empty_space() < total_size) {
        // TODO: keep a non-serialized message queue?
        dbglnc("Outgoing message buffer is full, dropping message");
        return;
    }

    connection->output_message_buffer.write({ &size, sizeof(u32) });
    connection->output_message_buffer.write(message->serialized);

    if (flush_output_buffer(connection).is_error()) {
        set_peer_errored(peer);
        return;
    }

    connection->last_message_sent_at = Core::DateTime::now();
}

ErrorOr<void> Comm::flush_output_buffer(NonnullRefPtr<PeerConnection> connection)
{
    // VERIFY(peer->output_message_buffer.used_space() > 0);
    if (connection->output_message_buffer.used_space() == 0) {
        dbglnc("Nothing to flush!");
    }

    for (;;) {
        auto err = connection->output_message_buffer.flush_to_stream(*connection->socket);
        if (err.is_error()) {
            auto code = err.error().code();
            if (code == EINTR) {
                continue;
            } else if (code == EAGAIN) {
                dbglnc("Socket is not ready to write, enabling read to write notifier");
                connection->socket_writable_notifier->set_enabled(true);
            } else {
                dbglnc("Error writing to socket: err: {}  code: {}  codestr: {}", err.error(), code, strerror(code));
                return Error::from_errno(code);
            }
            return {};
        }
        connection->bytes_uploaded_since_last_speed_measurement += err.release_value();

        if (connection->output_message_buffer.used_space() == 0) {
            //            dbglncc(parent_context, context, "Output message buffer is empty, we sent everything, disabling ready to write notifier");
            connection->socket_writable_notifier->set_enabled(false);
            return {};
        }
    }
}

void Comm::connect_more_peers(NonnullRefPtr<TorrentContext> torrent)
{
    u16 total_connections = 0;
    for (auto const& c : m_torrent_contexts) {
        total_connections += c.value->connected_peers.size();
    }
    size_t available_slots = min(max_connections_per_torrent - torrent->connected_peers.size(), max_total_connections - total_connections);
    dbglnc("We have {} available slots for new connections", available_slots);

    auto vector = torrent->all_peers.values();
    auto peer_it = vector.begin();
    while (available_slots > 0 && !peer_it.is_end()) {
        auto& peer = *peer_it;
        if (!peer->connected && !peer->errored) {
            m_peer_context_stack.append(peer);
            auto err = connect_to_peer(peer);
            if (err.is_error()) {
                dbglnc("Failed to initiate a connection for peer {}, error: {}", peer->address, err.error());
                peer->errored = true;
            } else {
                available_slots--;
            }
            m_peer_context_stack.remove(m_peer_context_stack.size() - 1);
        }
        peer_it++;
    }
}

ErrorOr<void> Comm::connect_to_peer(NonnullRefPtr<PeerContext> peer)
{
    auto socket_fd = TRY(Core::System::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0));

    auto sockaddr = peer->address.to_sockaddr_in();
    auto connect_err = Core::System::connect(socket_fd, bit_cast<struct sockaddr*>(&sockaddr), sizeof(sockaddr));
    if (connect_err.is_error() && connect_err.error().code() != EINPROGRESS)
        return connect_err;

    auto socket = TRY(Core::TCPSocket::adopt_fd(socket_fd));
    NonnullRefPtr<Core::Notifier> write_notifier = Core::Notifier::construct(socket_fd, Core::Notifier::Type::Write);
    auto connection = TRY(PeerConnection::try_create(socket, write_notifier, 5 * MiB, 5 * MiB, PeerRole::Server));

    write_notifier->on_activation = [&, socket_fd, connection, peer] {
        m_peer_context_stack.append(peer);

        // We were already connected and we can write again:
        if (peer->connected || m_connecting_peers.contains(connection)) {
            if (flush_output_buffer(connection).is_error())
                set_peer_errored(peer);
            m_peer_context_stack.clear();
            return;
        }

        // We were trying to connect, we can now figure out if it succeeded or not:
        int so_error;
        socklen_t len = sizeof(so_error);
        auto ret = getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &so_error, &len);
        if (ret == -1) {
            auto errn = errno;
            dbglnc("error calling getsockopt: errno:{} {}", errn, strerror(errn));
            m_peer_context_stack.clear();
            return;
        }
        // dbglnc(context, "getsockopt SO_ERROR resut: '{}' ({}) for peer {}", strerror(so_error), so_error, address.to_deprecated_string());

        if (so_error == ESUCCESS) {
            connection->socket_writable_notifier->set_enabled(false);

            m_connecting_peers.set(connection, peer);
            connection->socket->on_ready_to_read = [&, peer] {
                m_peer_context_stack.append(peer);
                auto err = read_from_socket(connection);
                if (err.is_error())
                    dbglnc("Error reading from socket: {}", err.error());
                m_peer_context_stack.clear();
            };

            dbglnc("Connected, sending handshake");
            if (send_handshake(peer->torrent_context->info_hash, peer->torrent_context->local_peer_id, connection).is_error()) {
                m_connecting_peers.remove(connection);
                close_connection(connection);
                peer->errored = true;
            }
        } else {
            // Would be nice to have GNU extension strerrorname_np() so we can print ECONNREFUSED,... too.
            dbglnc("Error connecting: {}", strerror(so_error));
            m_connecting_peers.remove(connection);
            close_connection(connection);
            peer->errored = true;
        }
        m_peer_context_stack.clear();
    };
    return {};
}

ErrorOr<void> Comm::send_handshake(InfoHash info_hash, PeerId local_peer_id, NonnullRefPtr<PeerConnection> connection)
{
    auto handshake = HandshakeMessage(info_hash, local_peer_id);
    dbglnc("Sending handshake: {}", handshake.to_string());
    connection->output_message_buffer.write({ &handshake, sizeof(handshake) });
    TRY(flush_output_buffer(connection));
    connection->handshake_sent = true;
    return {};
}

// TODO: rename to disconnect_peer(peer, error_flags) ?
void Comm::set_peer_errored(NonnullRefPtr<PeerContext> peer, bool should_connect_more_peers)
{
    auto torrent = peer->torrent_context;
    for (auto const& piece_index : peer->interesting_pieces) {
        torrent->missing_pieces.get(piece_index).value()->havers.remove(peer);
    }

    auto& piece = peer->incoming_piece;
    if (piece.index.has_value()) {
        insert_piece_in_heap(torrent, piece.index.value());
        piece.index = {};
    }

    peer->active = false;
    peer->connected = false;
    peer->errored = true;

    close_connection(peer->connection.release_nonnull());

    torrent->connected_peers.remove(peer);

    if (should_connect_more_peers)
        connect_more_peers(torrent);
}

void Comm::close_connection(NonnullRefPtr<PeerConnection> connection)
{
    connection->socket_writable_notifier->close();
    connection->socket->close();
    connection->socket->on_ready_to_read = nullptr;

    connection->download_speed = 0;
    connection->upload_speed = 0;
    connection->bytes_downloaded_since_last_speed_measurement = 0;
    connection->bytes_uploaded_since_last_speed_measurement = 0;
}

u64 Comm::get_available_peers_count(NonnullRefPtr<TorrentContext> torrent) const
{
    u64 count = 0;
    for (auto const& peer : torrent->all_peers) {
        if (!peer->connected && !peer->errored)
            count++;
    }
    return count;
}

ErrorOr<void> Comm::on_ready_to_accept()
{
    auto accepted = TRY(m_server->accept());
    TRY(accepted->set_blocking(false));

    NonnullRefPtr<Core::Notifier> write_notifier = Core::Notifier::construct(accepted->fd(), Core::Notifier::Type::Write);
    auto connection = TRY(PeerConnection::try_create(accepted, write_notifier, 5 * MiB, 5 * MiB, PeerRole::Client));
    m_accepted_connections.set(connection);

    write_notifier->on_activation = [&, connection] {
        // m_peer_context_stack.append(peer);
        if (flush_output_buffer(connection).is_error())
            dbgln("Error flushing output buffer for accepted connection");
    };
    write_notifier->set_enabled(false);

    connection->socket->on_ready_to_read = [&, connection] {
        auto err = read_from_socket(connection);
        if (err.is_error())
            dbglnc("Error reading from (accepted) socket: {}", err.error());
    };
    return {};
}

}
