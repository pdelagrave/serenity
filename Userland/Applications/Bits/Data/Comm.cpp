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
        gettimeofday(&m_last_speed_measurement, nullptr);
        start_timer(3000);
        return m_event_loop->exec();
    },
        "Data thread"sv);
    m_thread->start();
}

ErrorOr<void> Comm::add_peers(AddPeersCommand command)
{
    return TRY(post_command(make<AddPeersCommand>(command)));
}

ErrorOr<void> Comm::activate_torrent(NonnullOwnPtr<ActivateTorrentCommand> command)
{
    return TRY(post_command(move(command)));
}

Optional<NonnullRefPtr<Data::TorrentContext>> Comm::get_torrent_context(ReadonlyBytes info_hash)
{
    auto x = m_torrent_contexts.get(info_hash);
    if (!x.has_value())
        return {};
    return *x.value();
}

Vector<NonnullRefPtr<Data::TorrentContext>> Comm::get_torrent_contexts()
{
    Vector<NonnullRefPtr<Data::TorrentContext>> torrents;
    for (auto& torrent : m_torrent_contexts)
        torrents.append(torrent.value);
    return torrents;
}

void Comm::custom_event(Core::CustomEvent& event)
{
    auto err = [&]() -> ErrorOr<void> {
        switch (static_cast<Command::Type>(event.custom_type())) {
        case Command::Type::ActivateTorrent:
            return handle_command_activate_torrent(static_cast<ActivateTorrentCommand const&>(event));
        case Command::Type::AddPeers:
            return handle_command_add_peers(static_cast<AddPeersCommand const&>(event));
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

void Comm::timer_event(Core::TimerEvent&)
{
    timeval current_time;
    timeval time_diff;
    gettimeofday(&current_time, nullptr);
    timersub(&current_time, &m_last_speed_measurement, &time_diff);
    auto time_diff_ms = time_diff.tv_sec * 1000 + time_diff.tv_usec / 1000;
    for (auto const& torrent : m_torrent_contexts) {
        u64 upload_speed = 0;
        u64 download_speed = 0;
        for (auto const& peer : torrent.value->connected_peers) {
            peer->download_speed = (peer->bytes_downloaded_since_last_speed_measurement / time_diff_ms) * 1000;
            peer->bytes_downloaded_since_last_speed_measurement = 0;
            download_speed += peer->download_speed;

            peer->upload_speed = (peer->bytes_uploaded_since_last_speed_measurement / time_diff_ms) * 1000;
            peer->bytes_uploaded_since_last_speed_measurement = 0;
            upload_speed += peer->upload_speed;
        }
        torrent.value->upload_speed = upload_speed;
        torrent.value->download_speed = download_speed;
    }
    m_last_speed_measurement = current_time;
}

ErrorOr<void> Comm::post_command(NonnullOwnPtr<Command> command)
{
    m_event_loop->post_event(*this, move(command));
    return {};
}

ErrorOr<void> Comm::handle_command_activate_torrent(ActivateTorrentCommand const& command)
{
    auto torrent = move(command.torrent_context);
    for (u64 i = 0; i < torrent->piece_count; i++) {
        if (!torrent->local_bitfield.get(i))
            torrent->missing_pieces.set(i, make_ref_counted<BK::PieceStatus>(i));
    }
    m_torrent_contexts.set(torrent->info_hash, move(torrent));
    return {};
}

ErrorOr<void> Comm::handle_command_add_peers(AddPeersCommand const& command)
{
    auto torrent = m_torrent_contexts.get(command.info_hash).value();
    for (auto const& peer_address : command.peers) {
        // TODO algo for dupes, deleted (tracker announce doesn't return one anymore and we're downloading/uploading from it?)
        auto peer = TRY(PeerContext::try_create(*torrent, peer_address, 1 * MiB));
        // assuming peers are added only once
        torrent->all_peers.set(move(peer));
    }
    connect_more_peers(*torrent);
    return {};
}

ErrorOr<void> Comm::handle_command_piece_downloaded(PieceDownloadedCommand const& command)
{
    auto peer = command.peer_context();
    auto torrent = peer->torrent_context;
    auto index = command.index();
    auto data = command.data();
    if (TRY(torrent->data_file_map->validate_hash(index, data))) {
        TRY(torrent->data_file_map->write_piece(index, data));

        torrent->local_bitfield.set(index, true);
        auto& havers = torrent->missing_pieces.get(index).value()->havers;
        dbglnc(peer, "Havers of that piece {} we just downloaded: {}", index, havers.size());
        for (auto& haver : havers) {
            VERIFY(haver->interesting_pieces.remove(index));
            dbglnc(peer, "Removed piece {} from interesting pieces of {}", index, haver);
            if (haver->interesting_pieces.is_empty()) {
                dbglnc(peer, "Peer {} has no more interesting pieces, sending a NotInterested message", haver);
                TRY(send_message(make<BitTorrent::NotInterested>(), haver));
                haver->we_are_interested_in_peer = false;
            }
        }
        torrent->missing_pieces.remove(index);

        dbglnc(peer, "We completed piece {}", index);

        for (auto const& connected_peer : torrent->connected_peers)
            TRY(send_message(make<BitTorrent::Have>(index), connected_peer));

    } else {
        insert_piece_in_heap(torrent, index);
        dbglnc(peer, "Piece {} failed hash check", index);
    }
    TRY(piece_or_peer_availability_updated(peer));
    return {};
}

ErrorOr<void> Comm::handle_bitfield(NonnullOwnPtr<BitTorrent::BitFieldMessage> bitfield, NonnullRefPtr<PeerContext> peer)
{
    //    auto bitfield_data_size = context->torrent->local_bitfield().data_size();
    //    if (bytes.size() != bitfield_data_size) {
    //        warnln("Bitfield sent by peer has a size ({}) different than expected({})", bytes.size(), bitfield_data_size);
    //        return Error::from_string_literal("Bitfield sent by peer has a size different from expected");
    //    }

    peer->bitfield = bitfield->bitfield;
    dbglnc(peer, "Set bitfield for peer. size: {} data_size: {}", peer->bitfield.size(), peer->bitfield.data_size());

    for (auto& missing_piece : peer->torrent_context->missing_pieces.keys()) {
        if (peer->bitfield.get(missing_piece))
            TRY(update_piece_availability(missing_piece, peer));
    }
    return {};
}

ErrorOr<void> Comm::handle_handshake(NonnullOwnPtr<BitTorrent::Handshake> handshake, NonnullRefPtr<PeerContext> peer)
{
    dbglnc(peer, "Received handshake_message: Protocol: {}, Reserved: {:08b} {:08b} {:08b} {:08b} {:08b} {:08b} {:08b} {:08b}, info_hash: {:20hex-dump}, peer_id: {:20hex-dump}",
        handshake->pstr,
        handshake->reserved[0],
        handshake->reserved[1],
        handshake->reserved[2],
        handshake->reserved[3],
        handshake->reserved[4],
        handshake->reserved[5],
        handshake->reserved[6],
        handshake->reserved[7],
        handshake->info_hash,
        handshake->peer_id);

    if (peer->torrent_context->info_hash != Bytes { handshake->info_hash, 20 }) {
        dbglnc(peer, "Peer sent a handshake with the wrong info hash.");
        return Error::from_string_literal("Peer sent a handshake with the wrong info hash.");
    }
    peer->got_handshake = true;
    peer->incoming_message_length = 0;

    return {};
}

ErrorOr<void> Comm::handle_have(NonnullOwnPtr<BitTorrent::Have> have_message, NonnullRefPtr<PeerContext> peer)
{
    auto piece_index = have_message->piece_index;
    dbglnc(peer, "Peer has piece {}, setting in peer bitfield, bitfield size: {}", piece_index, peer->bitfield.size());
    peer->bitfield.set(piece_index, true);
    TRY(update_piece_availability(piece_index, peer));
    return {};
}

ErrorOr<void> Comm::handle_piece(NonnullOwnPtr<BitTorrent::Piece> piece_message, NonnullRefPtr<PeerContext> peer)
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
        TRY(post_command(make<PieceDownloadedCommand>(index, piece.data.bytes(), peer)));
        piece.index = {};
        peer->active = false;
    } else {
        if (peer->peer_is_choking_us) {
            dbglnc(peer, "Weren't done downloading the blocks for this piece {}, but peer is choking us, so we're giving up on it", index);
            piece.index = {};
            peer->active = false;
            insert_piece_in_heap(torrent, index);
            TRY(piece_or_peer_availability_updated(peer));
        } else {
            auto next_block_length = min((size_t)BlockLength, (size_t)piece.length - piece.offset);
            // dbglnc(context, "Sending next request for piece {} at offset {}/{} blocklen: {}", index, piece.offset, piece.length, next_block_length);
            TRY(send_message(make<BitTorrent::Request>(index, piece.offset, next_block_length), peer));
        }
    }
    return {};
}

ErrorOr<void> Comm::read_from_socket(NonnullRefPtr<PeerContext> peer)
{
    // TODO: cleanup this mess, read everything we can in a buffer at every call first.
    auto& socket = peer->socket;
    // dbglnc(context, "Reading from socket");

    // hack:
    if (TRY(socket->pending_bytes()) == 0) {
        dbglnc(peer, "Socket has no pending bytes, reading and writing to it to force receiving an RST");
        // remote host probably closed the connection, reading from the socket to force receiving an RST and having the connection being fully closed on our side.
        //        TRY(socket->read_some(ByteBuffer::create_uninitialized(1).release_value().bytes()));
        //        TRY(socket->write_value(BigEndian<u32>(0)));
        peer->socket_writable_notifier->close();
        socket->close();
        return {};
    }
    if (TRY(socket->can_read_without_blocking())) {
        if (peer->incoming_message_length == 0) {
            // dbglnc(context, "Incoming message length is 0");
            if (TRY(socket->pending_bytes()) >= 2) {
                // dbglnc(context, "Socket has at least 2 pending bytes");
                peer->incoming_message_length = TRY(socket->read_value<BigEndian<u32>>());
                if (peer->incoming_message_length == 0) {
                    dbglnc(peer, "Received keep-alive");
                    return {};
                } else {
                    peer->incoming_message_buffer.clear();
                    peer->incoming_message_buffer.ensure_capacity(peer->incoming_message_length);
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
        auto will_read = min(pending_bytes, peer->incoming_message_length - peer->incoming_message_buffer.size());
        //        dbglnc(context, "Will read {} bytes", will_read);
        TRY(socket->read_until_filled(TRY(peer->incoming_message_buffer.get_bytes_for_writing(will_read))));
        peer->bytes_downloaded_since_last_speed_measurement += will_read;

        if (peer->incoming_message_buffer.size() == peer->incoming_message_length) {
            auto message_stream = FixedMemoryStream(peer->incoming_message_buffer.bytes());
            if (!peer->got_handshake) {
                dbglnc(peer, "No handshake yet, trying to read and parse it");
                TRY(handle_handshake(TRY(BitTorrent::Handshake::try_create(message_stream)), peer));
                TRY(send_message(make<BitTorrent::BitFieldMessage>(BitField(peer->torrent_context->local_bitfield)), peer));
            } else {
                using MessageType = Bits::BitTorrent::Message::Type;
                auto message_type = TRY(message_stream.read_value<MessageType>());
                dbglnc(peer, "Got message type {}", TRY(Bits::BitTorrent::Message::to_string(message_type)));
                TRY(message_stream.seek(0, AK::SeekMode::SetPosition));
                peer->incoming_message_length = 0;

                switch (message_type) {
                case MessageType::Choke:
                    peer->peer_is_choking_us = true;
                    TRY(piece_or_peer_availability_updated(peer));
                    break;
                case MessageType::Unchoke:
                    peer->peer_is_choking_us = false;
                    TRY(piece_or_peer_availability_updated(peer));
                    break;
                case MessageType::Interested:
                    peer->peer_is_interested_in_us = true;
                    break;
                case MessageType::NotInterested:
                    peer->peer_is_interested_in_us = false;
                    break;
                case MessageType::Have:
                    TRY(handle_have(make<BitTorrent::Have>(message_stream), peer));
                    break;
                case MessageType::Bitfield:
                    TRY(handle_bitfield(make<BitTorrent::BitFieldMessage>(message_stream), peer));
                    break;
                case MessageType::Request:
                    dbglnc(peer, "ERROR: Message type Request is unsupported");
                    break;
                case MessageType::Piece:
                    TRY(handle_piece(make<BitTorrent::Piece>(message_stream), peer));
                    break;
                case MessageType::Cancel:
                    dbglnc(peer, "ERROR: message type Cancel is unsupported");
                    break;
                default:
                    dbglnc(peer, "ERROR: Got unsupported message type: {:02X}: {}", (u8)message_type, TRY(BitTorrent::Message::to_string(message_type)));
                    break;
                }
            }
        }
    }
    return {};
}

ErrorOr<void> Comm::send_message(NonnullOwnPtr<BitTorrent::Message> message, NonnullRefPtr<PeerContext> context, RefPtr<PeerContext> parent_context)
{
    dbglncc(parent_context, context, "Sending {}", *message);
    size_t size_to_send = message->size() + sizeof(u32); // message size + message payload
    if (context->output_message_buffer.empty_space() < size_to_send) {
        // TODO: keep a non-serialized message queue?
        dbglncc(parent_context, context, "Outgoing message buffer is full, dropping message");
        return {};
    }
    auto size = BigEndian<u32>(message->size());
    context->output_message_buffer.write({ &size, sizeof(u32) });
    context->output_message_buffer.write(message->serialized);
    flush_output_buffer(context, parent_context);
    return {};
}

void Comm::flush_output_buffer(NonnullRefPtr<PeerContext> peer, RefPtr<PeerContext> parent_context)
{
    VERIFY(peer->output_message_buffer.used_space() > 0);
    for (;;) {
        auto err = peer->output_message_buffer.flush_to_stream(*peer->socket);
        if (err.is_error()) {
            if (err.error().code() == EAGAIN || err.error().code() == EWOULDBLOCK || err.error().code() == EINTR) {
                dbglncc(parent_context, peer, "Socket is not ready to write, enabling read to write notifier");
                peer->socket_writable_notifier->set_enabled(true);
            } else {
                dbglncc(parent_context, peer, "Error writing to socket: err: {}  code: {}  codestr: {}", err.error(), err.error().code(), strerror(err.error().code()));
                set_peer_errored(peer);
            }
            return;
        }
        peer->bytes_uploaded_since_last_speed_measurement += err.release_value();

        if (peer->output_message_buffer.used_space() == 0) {
            //            dbglncc(parent_context, context, "Output message buffer is empty, we sent everything, disabling ready to write notifier");
            peer->socket_writable_notifier->set_enabled(false);
            return;
        }
    }
}

void Comm::connect_more_peers(NonnullRefPtr<TorrentContext> torrent, RefPtr<PeerContext> forlogging)
{
    u16 total_connections = 0;
    for (auto const& c : m_torrent_contexts) {
        total_connections += c.value->connected_peers.size();
    }
    size_t available_slots = min(max_total_connections_per_torrent - torrent->connected_peers.size(), max_total_connections - total_connections);
    dbglnc(forlogging, "We have {} available slots for new connections", available_slots);

    auto vector = torrent->all_peers.values();
    auto peer_it = vector.begin();
    while (available_slots > 0 && !peer_it.is_end()) {
        auto& peer = *peer_it;
        if (!peer->connected && !peer->errored) {
            auto err = connect_to_peer(peer);
            if (err.is_error()) {
                dbglnc(forlogging, "Failed to initiate a connection for peer {}, error: {}", peer->address, err.error());
                peer->errored = true;
            } else {
                available_slots--;
            }
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

    peer->socket_writable_notifier = Core::Notifier::construct(socket_fd, Core::Notifier::Type::Write);

    auto socket = TRY(Core::TCPSocket::adopt_fd(socket_fd));
    peer->socket = move(socket);

    peer->socket_writable_notifier->on_activation = [&, socket_fd, peer] {
        if (peer->connected) {
            flush_output_buffer(peer);
            return;
        }

        int so_error;
        socklen_t len = sizeof(so_error);
        auto ret = getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &so_error, &len);
        if (ret == -1) {
            auto errn = errno;
            dbglnc(peer, "error calling getsockopt: errno:{} {}", errn, strerror(errn));
            return;
        }
        // dbglnc(context, "getsockopt SO_ERROR resut: '{}' ({}) for peer {}", strerror(so_error), so_error, address.to_deprecated_string());

        if (so_error == ESUCCESS) {
            dbglnc(peer, "Connection succeeded, sending handshake");
            peer->socket_writable_notifier->set_enabled(false);

            peer->connected = true;
            peer->torrent_context->connected_peers.set(peer);
            peer->socket->on_ready_to_read = [&, peer] {
                auto err = read_from_socket(peer);
                if (err.is_error()) {
                    dbglnc(peer, "Error reading from socket: {}", err.error());
                    return;
                }
            };

            auto handshake = BitTorrent::Handshake(peer->torrent_context->info_hash, peer->torrent_context->local_peer_id);
            peer->output_message_buffer.write({ &handshake, sizeof(handshake) });
            flush_output_buffer(peer);
            return;
        } else {
            // Would be nice to have GNU extension strerrorname_np() so we can print ECONNREFUSED,... too.
            dbglnc(peer, "Error connecting: {}", strerror(so_error));
            peer->socket_writable_notifier->close();
            peer->socket->close();
            peer->errored = true;
        }
    };
    return {};
}

void Comm::set_peer_errored(NonnullRefPtr<PeerContext> peer)
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
    
    peer->socket_writable_notifier->close();
    peer->socket->close();

    torrent->connected_peers.remove(peer);
    
    peer->download_speed = 0;
    peer->upload_speed = 0;
    peer->bytes_downloaded_since_last_speed_measurement = 0;
    peer->bytes_uploaded_since_last_speed_measurement = 0;
    
    connect_more_peers(torrent);
}

ErrorOr<void> Comm::piece_or_peer_availability_updated(RefPtr<PeerContext> forlogging)
{
    auto torrent = forlogging->torrent_context;

    size_t available_slots = 0;
    for (auto const& peer : torrent->connected_peers)
        available_slots += !peer->active;

    dbglnc(forlogging, "We have {} inactive peers out of {} connected peers.", available_slots, torrent->connected_peers.size());
    for (size_t i = 0; i < available_slots; i++) {
        dbglnc(forlogging, "Trying to start a piece download on a {}th peer", i);
        if (torrent->piece_heap.is_empty())
            return {};

        // TODO find out the rarest available piece, because the rarest piece might not be available right now.
        auto next_piece_index = torrent->piece_heap.peek_min()->index_in_torrent;
        dbglnc(forlogging, "Picked next piece for download {}", next_piece_index);
        // TODO improve how we select the peer. Choking algo, bandwidth, etc
        bool found_peer = false;
        for (auto& haver : torrent->missing_pieces.get(next_piece_index).value()->havers) {
            VERIFY(haver->connected);

            dbglnc(forlogging, "Peer {} is choking us: {}, is active: {}", haver, haver->peer_is_choking_us, haver->active);
            if (!haver->peer_is_choking_us && !haver->active) {
                dbglncc(forlogging, *haver, "Requesting piece {} from peer {}", next_piece_index, haver);
                haver->active = true;
                u32 block_length = min(BlockLength, torrent->piece_length(next_piece_index));
                TRY(send_message(make<BitTorrent::Request>(next_piece_index, 0, block_length), *haver, forlogging));

                found_peer = true;
                break;
            }
        }
        if (found_peer) {
            dbglnc(forlogging, "Found peer for piece {}, popping the piece from the heap", next_piece_index);
            auto piece_status = torrent->piece_heap.pop_min();
            piece_status->currently_downloading = true;
            VERIFY(piece_status->index_in_torrent == next_piece_index);
        } else {
            dbglnc(forlogging, "No more available peer to download piece {}", next_piece_index);
            break;
        }
    }
    return {};
}

ErrorOr<bool> Comm::update_piece_availability(u64 piece_index, NonnullRefPtr<PeerContext> peer)
{
    auto& torrent = peer->torrent_context;
    auto is_missing = torrent->missing_pieces.get(piece_index);
    if (!is_missing.has_value()) {
        dbglnc(peer, "Peer has piece {} but we already have it.", piece_index);
        return false;
    }

    auto& piece_status = is_missing.value();
    piece_status->havers.set(peer);
    if (!piece_status->currently_downloading) {
        if (piece_status->index_in_heap.has_value()) {
            torrent->piece_heap.update(*piece_status);
        } else {
            torrent->piece_heap.insert(*piece_status);
        }
    }

    peer->interesting_pieces.set(piece_index);

    if (!peer->we_are_interested_in_peer) {
        // TODO: figure out when is the best time to unchoke the peer.
        TRY(send_message(make<BitTorrent::Unchoke>(), peer));
        peer->we_are_choking_peer = false;

        TRY(send_message(make<BitTorrent::Interested>(), peer));
        peer->we_are_interested_in_peer = true;

        TRY(piece_or_peer_availability_updated(peer));
    }
    return true;
}

void Comm::insert_piece_in_heap(NonnullRefPtr<TorrentContext> torrent, u64 piece_index)
{
    auto piece_status = torrent->missing_pieces.get(piece_index).value();
    piece_status->currently_downloading = false;
    torrent->piece_heap.insert(*piece_status);
}

}
