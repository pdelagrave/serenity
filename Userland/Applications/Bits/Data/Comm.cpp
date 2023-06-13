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
        "Comm thread"sv);
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
    m_peer_context_stack.clear();
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
        auto peer = TRY(PeerContext::try_create(*torrent, peer_address, 10 * MiB, 1 * MiB));
        // assuming peers are added only once
        torrent->all_peers.set(move(peer));
    }
    connect_more_peers(*torrent);
    return {};
}

ErrorOr<void> Comm::handle_command_piece_downloaded(PieceDownloadedCommand const& command)
{
    auto peer = command.peer_context();
    m_peer_context_stack.append(peer);
    auto torrent = peer->torrent_context;
    auto index = command.index();
    auto data = command.data();
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
                send_message(make<BitTorrent::NotInterested>(), haver);
                haver->we_are_interested_in_peer = false;
            }
        }
        torrent->missing_pieces.remove(index);

        dbglnc("We completed piece {}", index);

        for (auto const& connected_peer : torrent->connected_peers) {
            m_peer_context_stack.append(connected_peer);
            send_message(make<BitTorrent::Have>(index), connected_peer);
            m_peer_context_stack.remove(m_peer_context_stack.size() - 1);
        }

    } else {
        insert_piece_in_heap(torrent, index);
        dbglnc("Piece {} failed hash check", index);
    }
    TRY(piece_or_peer_availability_updated(torrent));
    return {};
}

ErrorOr<void> Comm::handle_bitfield(NonnullOwnPtr<BitTorrent::BitFieldMessage> bitfield, NonnullRefPtr<PeerContext> peer)
{
    peer->bitfield = bitfield->bitfield;
    dbglnc("Set bitfield for peer. size: {} data_size: {}", peer->bitfield.size(), peer->bitfield.data_size());

    for (auto& missing_piece : peer->torrent_context->missing_pieces.keys()) {
        if (peer->bitfield.get(missing_piece))
            TRY(update_piece_availability(missing_piece, peer));
    }
    return {};
}

ErrorOr<void> Comm::handle_handshake(NonnullOwnPtr<BitTorrent::Handshake> handshake, NonnullRefPtr<PeerContext> peer)
{
    dbglnc("Received handshake_message: Protocol: {}, Reserved: {:08b} {:08b} {:08b} {:08b} {:08b} {:08b} {:08b} {:08b}, info_hash: {:20hex-dump}, peer_id: {:20hex-dump}",
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
        dbglnc("Peer sent a handshake with the wrong info hash.");
        return Error::from_string_literal("Peer sent a handshake with the wrong info hash.");
    }
    peer->got_handshake = true;
    peer->incoming_message_length = 0;

    return {};
}

ErrorOr<void> Comm::handle_have(NonnullOwnPtr<BitTorrent::Have> have_message, NonnullRefPtr<PeerContext> peer)
{
    auto piece_index = have_message->piece_index;
    dbglnc("Peer has piece {}, setting in peer bitfield, bitfield size: {}", piece_index, peer->bitfield.size());
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
            dbglnc("Weren't done downloading the blocks for this piece {}, but peer is choking us, so we're giving up on it", index);
            piece.index = {};
            peer->active = false;
            insert_piece_in_heap(torrent, index);
            TRY(piece_or_peer_availability_updated(torrent));
        } else {
            auto next_block_length = min((size_t)BlockLength, (size_t)piece.length - piece.offset);
            send_message(make<BitTorrent::Request>(index, piece.offset, next_block_length), peer);
        }
    }
    return {};
}

ErrorOr<void> Comm::parse_input_message(SeekableStream& stream, NonnullRefPtr<PeerContext> peer)
{
    if (!peer->got_handshake) {
        dbglnc("No handshake yet, trying to read and parse it");
        TRY(handle_handshake(TRY(BitTorrent::Handshake::try_create(stream)), peer));
        send_message(make<BitTorrent::BitFieldMessage>(BitField(peer->torrent_context->local_bitfield)), peer);
        return {};
    }

    using MessageType = Bits::BitTorrent::Message::Type;
    auto message_type = TRY(stream.read_value<MessageType>());
    dbglnc("Got message type {}", Bits::BitTorrent::Message::to_string(message_type));
    TRY(stream.seek(0, AK::SeekMode::SetPosition));
    peer->incoming_message_length = 0;

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
        peer->peer_is_interested_in_us = true;
        break;
    case MessageType::NotInterested:
        peer->peer_is_interested_in_us = false;
        break;
    case MessageType::Have:
        TRY(handle_have(make<BitTorrent::Have>(stream), peer));
        break;
    case MessageType::Bitfield:
        TRY(handle_bitfield(make<BitTorrent::BitFieldMessage>(stream), peer));
        break;
    case MessageType::Request:
        dbglnc("ERROR: Message type Request is unsupported");
        break;
    case MessageType::Piece:
        TRY(handle_piece(make<BitTorrent::Piece>(stream), peer));
        break;
    case MessageType::Cancel:
        dbglnc("ERROR: message type Cancel is unsupported");
        break;
    default:
        dbglnc("ERROR: Got unsupported message type: {:02X}: {}", (u8)message_type, BitTorrent::Message::to_string(message_type));
        break;
    }
    return {};
}

ErrorOr<void> Comm::read_from_socket(NonnullRefPtr<PeerContext> peer)
{
    auto& socket = peer->socket;
    dbglnc("Socket pending bytes:{} iseof:{}", TRY(socket->pending_bytes()), socket->is_eof());

    for (;;) {
        auto nread_or_error = peer->input_message_buffer.fill_from_stream(*socket);
        if (socket->is_eof()) {
            dbglnc("Peer disconnected");
            set_peer_errored(peer);
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
        peer->bytes_downloaded_since_last_speed_measurement += nread_or_error.value();
    }

    while (peer->input_message_buffer.used_space() >= peer->incoming_message_length) {
        if (peer->incoming_message_length > 0) {
            auto buffer = TRY(ByteBuffer::create_uninitialized(peer->incoming_message_length));
            VERIFY(peer->input_message_buffer.read(buffer.bytes()).size() == peer->incoming_message_length);
            auto message_byte_stream = FixedMemoryStream(buffer.bytes());
            TRY(parse_input_message(message_byte_stream, peer));
            peer->incoming_message_length = 0;
        } else if (peer->input_message_buffer.used_space() >= 4) {
            peer->input_message_buffer.read({&peer->incoming_message_length, sizeof(peer->incoming_message_length)});
            if (peer->incoming_message_length == 0)
                dbglnc("Received keep-alive");
        } else {
            // Not enough bytes to read the length of the next message
            return {};
        }
    }
    return {};
}

void Comm::send_message(NonnullOwnPtr<BitTorrent::Message> message, NonnullRefPtr<PeerContext> peer)
{
    auto size = BigEndian<u32>(message->size());
    dbglnc("Sending message [{}b] {}", size, *message);
    size_t total_size = message->size() + sizeof(u32); // message size + message payload
    if (peer->output_message_buffer.empty_space() < total_size) {
        // TODO: keep a non-serialized message queue?
        dbglnc("Outgoing message buffer is full, dropping message");
        return;
    }

    peer->output_message_buffer.write({ &size, sizeof(u32) });
    peer->output_message_buffer.write(message->serialized);
    flush_output_buffer(peer);
    return;
}

void Comm::flush_output_buffer(NonnullRefPtr<PeerContext> peer)
{
    VERIFY(peer->output_message_buffer.used_space() > 0);
    for (;;) {
        auto err = peer->output_message_buffer.flush_to_stream(*peer->socket);
        if (err.is_error()) {
            auto code = err.error().code();
            if (code == EINTR) {
                continue;
            } else if (code == EAGAIN) {
                dbglnc("Socket is not ready to write, enabling read to write notifier");
                peer->socket_writable_notifier->set_enabled(true);
            } else {
                dbglnc("Error writing to socket: err: {}  code: {}  codestr: {}", err.error(), code, strerror(code));
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

void Comm::connect_more_peers(NonnullRefPtr<TorrentContext> torrent)
{
    u16 total_connections = 0;
    for (auto const& c : m_torrent_contexts) {
        total_connections += c.value->connected_peers.size();
    }
    size_t available_slots = min(max_total_connections_per_torrent - torrent->connected_peers.size(), max_total_connections - total_connections);
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

    peer->socket_writable_notifier = Core::Notifier::construct(socket_fd, Core::Notifier::Type::Write);

    auto socket = TRY(Core::TCPSocket::adopt_fd(socket_fd));
    peer->socket = move(socket);

    peer->socket_writable_notifier->on_activation = [&, socket_fd, peer] {
        m_peer_context_stack.append(peer);

        // We were already connected and we can write again:
        if (peer->connected) {
            flush_output_buffer(peer);
            m_peer_context_stack.clear();
            return;
        }

        // We were trying to connect and we can now figure out if it succeeded or not:
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
            dbglnc("Connection succeeded, sending handshake");
            peer->socket_writable_notifier->set_enabled(false);

            peer->connected = true;
            peer->torrent_context->connected_peers.set(peer);
            peer->socket->on_ready_to_read = [&, peer] {
                m_peer_context_stack.append(peer);
                auto err = read_from_socket(peer);
                if (err.is_error())
                    dbglnc("Error reading from socket: {}", err.error());
                m_peer_context_stack.clear();
            };

            auto handshake = BitTorrent::Handshake(peer->torrent_context->info_hash, peer->torrent_context->local_peer_id);
            peer->output_message_buffer.write({ &handshake, sizeof(handshake) });
            flush_output_buffer(peer);
        } else {
            // Would be nice to have GNU extension strerrorname_np() so we can print ECONNREFUSED,... too.
            dbglnc("Error connecting: {}", strerror(so_error));
            peer->socket_writable_notifier->close();
            peer->socket->close();
            peer->errored = true;
        }
        m_peer_context_stack.clear();
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
                send_message(make<BitTorrent::Request>(next_piece_index, 0, block_length), *haver);
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

ErrorOr<bool> Comm::update_piece_availability(u64 piece_index, NonnullRefPtr<PeerContext> peer)
{
    auto& torrent = peer->torrent_context;
    auto is_missing = torrent->missing_pieces.get(piece_index);
    if (!is_missing.has_value()) {
        dbglnc("Peer has piece {} but we already have it.", piece_index);
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
        send_message(make<BitTorrent::Unchoke>(), peer);
        peer->we_are_choking_peer = false;

        send_message(make<BitTorrent::Interested>(), peer);
        peer->we_are_interested_in_peer = true;

        TRY(piece_or_peer_availability_updated(torrent));
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
