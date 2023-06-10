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

ErrorOr<void> Comm::add_peers(AddPeersCommand command)
{
    return TRY(post_command(make<AddPeersCommand>(command)));
}

ErrorOr<void> Comm::activate_torrent(NonnullOwnPtr<ActivateTorrentCommand> command)
{
    return TRY(post_command(move(command)));
}

Optional<NonnullRefPtr<Data::TorrentContext>> Comm::get_torrent_context(ReadonlyBytes info_hash) {
    auto x = m_tcontexts.get(info_hash);
    if (!x.has_value())
        return {};
    return *x.value();
}

Vector<NonnullRefPtr<Data::TorrentContext>> Comm::get_torrent_contexts()
{
    Vector<NonnullRefPtr<Data::TorrentContext>> tcontexts;
    for (auto& tcontext : m_tcontexts)
        tcontexts.append(tcontext.value);
    return tcontexts;
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

ErrorOr<void> Comm::post_command(NonnullOwnPtr<Command> command)
{
    m_event_loop->post_event(*this, move(command));
    return {};
}

ErrorOr<void> Comm::handle_command_activate_torrent(ActivateTorrentCommand const& command)
{
    auto tcontext = move(command.torrent_context);
    for (u64 i = 0; i < tcontext->piece_count; i++) {
        if (!tcontext->local_bitfield.get(i))
            tcontext->missing_pieces.set(i, make_ref_counted<BK::PieceStatus>(i));
    }
    m_tcontexts.set(tcontext->info_hash, move(tcontext));
    return {};
}

ErrorOr<void> Comm::handle_command_add_peers(AddPeersCommand const& command)
{
    auto tcontext = m_tcontexts.get(command.info_hash).value();
    for (auto const& peer_address : command.peers) {
        // TODO algo for dupes, deleted (tracker announce doesn't return one anymore and we're downloading/uploading from it?)
        auto pcontext = TRY(PeerContext::try_create(*tcontext, peer_address, 1 * MiB));
        // assuming peers are added only once
        tcontext->all_peers.set(move(pcontext));
    }
    connect_more_peers(*tcontext);
    return {};
}

ErrorOr<void> Comm::handle_command_piece_downloaded(PieceDownloadedCommand const& command)
{
    auto pcontext = command.peer_context();
    auto tcontext = pcontext->torrent_context;
    auto index = command.index();
    auto data = command.data();
    if (TRY(tcontext->data_file_map->validate_hash(index, data))) {
        TRY(tcontext->data_file_map->write_piece(index, data));

        tcontext->local_bitfield.set(index, true);
        auto& havers = tcontext->missing_pieces.get(index).value()->havers;
        dbglnc(pcontext, "Havers of that piece {} we just downloaded: {}", index, havers.size());
        for (auto& haver : havers) {
            VERIFY(haver->interesting_pieces.remove(index));
            dbglnc(pcontext, "Removed piece {} from interesting pieces of {}", index, haver);
            if (haver->interesting_pieces.is_empty()) {
                dbglnc(pcontext, "Peer {} has no more interesting pieces, sending a NotInterested message", haver);
                TRY(send_message(make<BitTorrent::NotInterested>(), haver));
                haver->we_are_interested_in_peer = false;
            }
        }
        tcontext->missing_pieces.remove(index);

        dbglnc(pcontext, "We completed piece {}", index);

        for (auto const& connected_peer : tcontext->connected_peers)
            TRY(send_message(make<BitTorrent::Have>(index), connected_peer));

    } else {
        TRY(insert_piece_in_heap(tcontext, index));
        dbglnc(pcontext, "Piece {} failed hash check", index);
    }
    TRY(piece_or_peer_availability_updated(pcontext));
    return {};
}

ErrorOr<void> Comm::handle_bitfield(NonnullOwnPtr<BitTorrent::BitFieldMessage> bitfield, NonnullRefPtr<PeerContext> pcontext)
{
    //    auto bitfield_data_size = context->torrent->local_bitfield().data_size();
    //    if (bytes.size() != bitfield_data_size) {
    //        warnln("Bitfield sent by peer has a size ({}) different than expected({})", bytes.size(), bitfield_data_size);
    //        return Error::from_string_literal("Bitfield sent by peer has a size different from expected");
    //    }

    pcontext->bitfield = bitfield->bitfield;
    dbglnc(pcontext, "Set bitfield for peer. size: {} data_size: {}", pcontext->bitfield.size(), pcontext->bitfield.data_size());

    for (auto& missing_piece : pcontext->torrent_context->missing_pieces.keys()) {
        if (pcontext->bitfield.get(missing_piece))
            TRY(update_piece_availability(missing_piece, pcontext));
    }
    return {};
}

ErrorOr<void> Comm::handle_handshake(NonnullOwnPtr<BitTorrent::Handshake> handshake, NonnullRefPtr<PeerContext> pcontext)
{
    dbglnc(pcontext, "Received handshake_message: Protocol: {}, Reserved: {:08b} {:08b} {:08b} {:08b} {:08b} {:08b} {:08b} {:08b}, info_hash: {:20hex-dump}, peer_id: {:20hex-dump}",
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

    if (pcontext->torrent_context->info_hash != Bytes { handshake->info_hash, 20 }) {
        dbglnc(pcontext, "Peer sent a handshake with the wrong info hash.");
        return Error::from_string_literal("Peer sent a handshake with the wrong info hash.");
    }
    pcontext->got_handshake = true;
    pcontext->incoming_message_length = 0;

    return {};
}

ErrorOr<void> Comm::handle_have(NonnullOwnPtr<BitTorrent::Have> have_message, NonnullRefPtr<PeerContext> pcontext)
{
    auto piece_index = have_message->piece_index;
    dbglnc(pcontext, "Peer has piece {}, setting in peer bitfield, bitfield size: {}", piece_index, pcontext->bitfield.size());
    pcontext->bitfield.set(piece_index, true);
    TRY(update_piece_availability(piece_index, pcontext));
    return {};
}

ErrorOr<void> Comm::handle_piece(NonnullOwnPtr<BitTorrent::Piece> piece_message, NonnullRefPtr<PeerContext> pcontext)
{
    auto tcontext = pcontext->torrent_context;
    auto block_size = piece_message->block.size();
    auto index = piece_message->piece_index;
    auto begin = piece_message->begin_offset;

    auto& piece = pcontext->incoming_piece;
    if (piece.index.has_value()) {
        VERIFY(index == piece.index);
        VERIFY(begin == piece.offset);
    } else {
        VERIFY(begin == 0);
        piece.index = index;
        piece.offset = 0;
        piece.length = tcontext->piece_length(index);
        piece.data.resize(piece.length);
    }

    piece.data.overwrite(begin, piece_message->block.bytes().data(), block_size);
    piece.offset = begin + block_size;
    if (piece.offset == (size_t)piece.length) {
        TRY(post_command(make<PieceDownloadedCommand>(index, piece.data.bytes(), pcontext)));
        piece.index = {};
        pcontext->active = false;
    } else {
        if (pcontext->peer_is_choking_us) {
            dbglnc(pcontext, "Weren't done downloading the blocks for this piece {}, but peer is choking us, so we're giving up on it", index);
            piece.index = {};
            pcontext->active = false;
            TRY(insert_piece_in_heap(tcontext, index));
            TRY(piece_or_peer_availability_updated(pcontext));
        } else {
            auto next_block_length = min((size_t)BlockLength, (size_t)piece.length - piece.offset);
            // dbglnc(context, "Sending next request for piece {} at offset {}/{} blocklen: {}", index, piece.offset, piece.length, next_block_length);
            TRY(send_message(make<BitTorrent::Request>(index, piece.offset, next_block_length), pcontext));
        }
    }
    return {};
}

ErrorOr<void> Comm::read_from_socket(NonnullRefPtr<PeerContext> pcontext)
{
    // TODO: cleanup this mess, read everything we can in a buffer at every call first.
    auto& socket = pcontext->socket;
    // dbglnc(context, "Reading from socket");

    // hack:
    if (TRY(socket->pending_bytes()) == 0) {
        dbglnc(pcontext, "Socket has no pending bytes, reading and writing to it to force receiving an RST");
        // remote host probably closed the connection, reading from the socket to force receiving an RST and having the connection being fully closed on our side.
        //        TRY(socket->read_some(ByteBuffer::create_uninitialized(1).release_value().bytes()));
        //        TRY(socket->write_value(BigEndian<u32>(0)));
        pcontext->socket_writable_notifier->close();
        socket->close();
        return {};
    }
    if (TRY(socket->can_read_without_blocking())) {
        if (pcontext->incoming_message_length == 0) {
            // dbglnc(context, "Incoming message length is 0");
            if (TRY(socket->pending_bytes()) >= 2) {
                // dbglnc(context, "Socket has at least 2 pending bytes");
                pcontext->incoming_message_length = TRY(socket->read_value<BigEndian<u32>>());
                if (pcontext->incoming_message_length == 0) {
                    dbglnc(pcontext, "Received keep-alive");
                    return {};
                } else {
                    pcontext->incoming_message_buffer.clear();
                    pcontext->incoming_message_buffer.ensure_capacity(pcontext->incoming_message_length);
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
        auto will_read = min(pending_bytes, pcontext->incoming_message_length - pcontext->incoming_message_buffer.size());
        //        dbglnc(context, "Will read {} bytes", will_read);
        TRY(socket->read_until_filled(TRY(pcontext->incoming_message_buffer.get_bytes_for_writing(will_read))));

        if (pcontext->incoming_message_buffer.size() == pcontext->incoming_message_length) {
            auto message_stream = FixedMemoryStream(pcontext->incoming_message_buffer.bytes());
            if (!pcontext->got_handshake) {
                dbglnc(pcontext, "No handshake yet, trying to read and parse it");
                TRY(handle_handshake(TRY(BitTorrent::Handshake::try_create(message_stream)), pcontext));
                TRY(send_message(make<BitTorrent::BitFieldMessage>(BitField(pcontext->torrent_context->local_bitfield)), pcontext));
            } else {
                using MessageType = Bits::BitTorrent::Message::Type;
                auto message_type = TRY(message_stream.read_value<MessageType>());
                dbglnc(pcontext, "Got message type {}", TRY(Bits::BitTorrent::Message::to_string(message_type)));
                TRY(message_stream.seek(0, AK::SeekMode::SetPosition));
                pcontext->incoming_message_length = 0;

                switch (message_type) {
                case MessageType::Choke:
                    pcontext->peer_is_choking_us = true;
                    TRY(piece_or_peer_availability_updated(pcontext));
                    break;
                case MessageType::Unchoke:
                    pcontext->peer_is_choking_us = false;
                    TRY(piece_or_peer_availability_updated(pcontext));
                    break;
                case MessageType::Interested:
                    pcontext->peer_is_interested_in_us = true;
                    break;
                case MessageType::NotInterested:
                    pcontext->peer_is_interested_in_us = false;
                    break;
                case MessageType::Have:
                    TRY(handle_have(make<BitTorrent::Have>(message_stream), pcontext));
                    break;
                case MessageType::Bitfield:
                    TRY(handle_bitfield(make<BitTorrent::BitFieldMessage>(message_stream), pcontext));
                    break;
                case MessageType::Request:
                    dbglnc(pcontext, "ERROR: Message type Request is unsupported");
                    break;
                case MessageType::Piece:
                    TRY(handle_piece(make<BitTorrent::Piece>(message_stream), pcontext));
                    break;
                case MessageType::Cancel:
                    dbglnc(pcontext, "ERROR: message type Cancel is unsupported");
                    break;
                default:
                    dbglnc(pcontext, "ERROR: Got unsupported message type: {:02X}: {}", (u8)message_type, TRY(BitTorrent::Message::to_string(message_type)));
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

void Comm::flush_output_buffer(NonnullRefPtr<PeerContext> pcontext, RefPtr<PeerContext> parent_context)
{
    VERIFY(pcontext->output_message_buffer.used_space() > 0);
    for (;;) {
        auto err = pcontext->output_message_buffer.flush_to_stream(*pcontext->socket);
        if (err.is_error()) {
            if (err.error().code() == EAGAIN || err.error().code() == EWOULDBLOCK || err.error().code() == EINTR) {
                dbglncc(parent_context, pcontext, "Socket is not ready to write, enabling read to write notifier");
                pcontext->socket_writable_notifier->set_enabled(true);
            } else {
                dbglncc(parent_context, pcontext, "Error writing to socket: err: {}  code: {}  codestr: {}", err.error(), err.error().code(), strerror(err.error().code()));
                set_peer_errored(pcontext);
            }
            return;
        }
        //        dbglncc(parent_context, context, "Wrote {} bytes to socket", err.value());

        if (pcontext->output_message_buffer.used_space() == 0) {
            //            dbglncc(parent_context, context, "Output message buffer is empty, we sent everything, disabling ready to write notifier");
            pcontext->socket_writable_notifier->set_enabled(false);
            return;
        }
    }
}

void Comm::connect_more_peers(NonnullRefPtr<TorrentContext> tcontext, RefPtr<PeerContext> forlogging)
{
    u16 total_connections = 0;
    for (auto const& c : m_tcontexts) {
        total_connections += c.value->connected_peers.size();
    }
    size_t available_slots = min(max_total_connections_per_torrent - tcontext->connected_peers.size(), max_total_connections - total_connections);
    dbglnc(forlogging, "We have {} available slots for new connections", available_slots);

    auto vector = tcontext->all_peers.values();
    auto peer_it = vector.begin();
    while (available_slots > 0 && !peer_it.is_end()) {
        auto& pcontext = *peer_it;
        if (!pcontext->connected && !pcontext->errored) {
            auto err = connect_to_peer(pcontext);
            if (err.is_error()) {
                dbglnc(forlogging, "Failed to initiate a connection for peer {}, error: {}", pcontext->address, err.error());
                pcontext->errored = true;
            } else {
                available_slots--;
            }
        }
        peer_it++;
    }
}

ErrorOr<void> Comm::connect_to_peer(NonnullRefPtr<PeerContext> pcontext)
{

    auto socket_fd = TRY(Core::System::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0));
    auto sockaddr = pcontext->address.to_sockaddr_in();
    auto connect_err = Core::System::connect(socket_fd, bit_cast<struct sockaddr*>(&sockaddr), sizeof(sockaddr));
    if (connect_err.is_error() && connect_err.error().code() != EINPROGRESS)
        return connect_err;

    pcontext->socket_writable_notifier = Core::Notifier::construct(socket_fd, Core::Notifier::Type::Write);

    auto socket = TRY(Core::TCPSocket::adopt_fd(socket_fd));
    pcontext->socket = move(socket);

    pcontext->socket_writable_notifier->on_activation = [&, socket_fd, pcontext] {
        if (pcontext->connected) {
            flush_output_buffer(pcontext);
            return;
        }

        int so_error;
        socklen_t len = sizeof(so_error);
        auto ret = getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &so_error, &len);
        if (ret == -1) {
            auto errn = errno;
            dbglnc(pcontext, "error calling getsockopt: errno:{} {}", errn, strerror(errn));
            return;
        }
        // dbglnc(context, "getsockopt SO_ERROR resut: '{}' ({}) for peer {}", strerror(so_error), so_error, address.to_deprecated_string());

        if (so_error == ESUCCESS) {
            dbglnc(pcontext, "Connection succeeded, sending handshake");
            pcontext->socket_writable_notifier->set_enabled(false);

            pcontext->connected = true;
            pcontext->torrent_context->connected_peers.set(pcontext);
            pcontext->socket->on_ready_to_read = [&, pcontext] {
                auto err = read_from_socket(pcontext);
                if (err.is_error()) {
                    dbglnc(pcontext, "Error reading from socket: {}", err.error());
                    return;
                }
            };

            auto handshake = BitTorrent::Handshake(pcontext->torrent_context->info_hash, pcontext->torrent_context->local_peer_id);
            pcontext->output_message_buffer.write({ &handshake, sizeof(handshake) });
            flush_output_buffer(pcontext);
            return;
        } else {
            // Would be nice to have GNU extension strerrorname_np() so we can print ECONNREFUSED,... too.
            dbglnc(pcontext, "Error connecting: {}", strerror(so_error));
            pcontext->socket_writable_notifier->close();
            pcontext->socket->close();
            pcontext->errored = true;
        }
    };
    return {};
}

void Comm::set_peer_errored(NonnullRefPtr<PeerContext> pcontext)
{
    // TODO cleanup the piece status for this peer
    pcontext->active = false;
    pcontext->connected = false;
    pcontext->errored = true;
    pcontext->socket_writable_notifier->close();
    pcontext->socket->close();
    pcontext->torrent_context->connected_peers.remove(pcontext);
    connect_more_peers(pcontext->torrent_context);
}

ErrorOr<void> Comm::piece_or_peer_availability_updated(RefPtr<PeerContext> pcontext)
{
    auto tcontext = pcontext->torrent_context;

    size_t available_slots = 0;
    for (auto const& peer : tcontext->connected_peers)
        available_slots += !peer->active;

    dbglnc(pcontext, "We have {} inactive peers out of {} connected peers.", available_slots, tcontext->connected_peers.size());
    for (size_t i = 0; i < available_slots; i++) {
        dbglnc(pcontext, "Trying to start a piece download on a {}th peer", i);
        if (tcontext->piece_heap.is_empty())
            return {};

        // TODO find out the rarest available piece, because the rarest piece might not be available right now.
        auto next_piece_index = tcontext->piece_heap.peek_min()->index_in_torrent;
        dbglnc(pcontext, "Picked next piece for download {}", next_piece_index);
        // TODO improve how we select the peer. Choking algo, bandwidth, etc
        bool found_peer = false;
        for (auto& peer : tcontext->missing_pieces.get(next_piece_index).value()->havers) {
            VERIFY(peer->connected);

            dbglnc(pcontext, "Peer {} is choking us: {}, is active: {}", peer, peer->peer_is_choking_us, peer->active);
            if (!peer->peer_is_choking_us && !peer->active) {
                dbglncc(pcontext, *peer, "Requesting piece {} from peer {}", next_piece_index, peer);
                peer->active = true;
                u32 block_length = min(BlockLength, tcontext->piece_length(next_piece_index));
                TRY(send_message(make<BitTorrent::Request>(next_piece_index, 0, block_length), *peer, pcontext));

                found_peer = true;
                break;
            }
        }
        if (found_peer) {
            dbglnc(pcontext, "Found peer for piece {}, popping the piece from the heap", next_piece_index);
            auto piece_status = tcontext->piece_heap.pop_min();
            piece_status->currently_downloading = true;
            VERIFY(piece_status->index_in_torrent == next_piece_index);
        } else {
            dbglnc(pcontext, "No more available peer to download piece {}", next_piece_index);
            break;
        }
    }
    return {};
}

ErrorOr<bool> Comm::update_piece_availability(u64 piece_index, NonnullRefPtr<PeerContext> pcontext)
{
    auto& tcontext = pcontext->torrent_context;
    auto is_missing = tcontext->missing_pieces.get(piece_index);
    if (!is_missing.has_value()) {
        dbglnc(pcontext, "Peer has piece {} but we already have it.", piece_index);
        return false;
    }

    auto& piece_status = is_missing.value();
    piece_status->havers.set(pcontext);
    if (!piece_status->currently_downloading) {
        if (piece_status->index_in_heap.has_value()) {
            tcontext->piece_heap.update(*piece_status);
        } else {
            tcontext->piece_heap.insert(*piece_status);
        }
    }

    pcontext->interesting_pieces.set(piece_index);

    if (!pcontext->we_are_interested_in_peer) {
        // TODO: figure out when is the best time to unchoke the peer.
        TRY(send_message(make<BitTorrent::Unchoke>(), pcontext));
        pcontext->we_are_choking_peer = false;

        TRY(send_message(make<BitTorrent::Interested>(), pcontext));
        pcontext->we_are_interested_in_peer = true;

        TRY(piece_or_peer_availability_updated(pcontext));
    }
    return true;
}

ErrorOr<void> Comm::insert_piece_in_heap(NonnullRefPtr<TorrentContext> tcontext, u64 piece_index)
{
    auto piece_status = tcontext->missing_pieces.get(piece_index).value();
    piece_status->currently_downloading = false;
    tcontext->piece_heap.insert(*piece_status);
    return {};
}

}
