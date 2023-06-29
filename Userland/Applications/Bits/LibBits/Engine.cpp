/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Engine.h"
#include <AK/LexicalPath.h>
#include <LibFileSystem/FileSystem.h>
#include <LibProtocol/Request.h>

namespace Bits {

void Engine::add_torrent(NonnullOwnPtr<MetaInfo> meta_info, DeprecatedString data_path)
{
    auto torrent_root_dir = meta_info->root_dir_name();
    DeprecatedString optional_root_dir = "";
    if (torrent_root_dir.has_value()) {
        optional_root_dir = DeprecatedString::formatted("/{}", torrent_root_dir.value());
    }

    auto root_data_path = DeprecatedString::formatted("{}{}", data_path, optional_root_dir);
    auto local_files = make<Vector<NonnullRefPtr<LocalFile>>>();
    for (auto f : meta_info->files()) {
        auto local_path = DeprecatedString::formatted("{}/{}", root_data_path, f->path());
        local_files->append(make_ref_counted<LocalFile>(move(local_path), move(f)));
    }

    NonnullRefPtr<Torrent> const& torrent = make_ref_counted<Torrent>(move(meta_info), move(local_files), root_data_path);
    auto info_hash = InfoHash({ torrent->meta_info().info_hash() });
    m_torrents.set(info_hash, move(torrent));
}

NonnullOwnPtr<HashMap<InfoHash, TorrentView>> Engine::torrents_views()
{
    auto views = make<HashMap<InfoHash, TorrentView>>();
    for (auto const& [info_hash, torrent] : m_torrents) {
        auto maybe_tcontext = m_torrent_contexts.get(info_hash);
        if (maybe_tcontext.has_value()) {
            auto tcontext = maybe_tcontext.release_value();
            Vector<PeerView> pviews;
            pviews.ensure_capacity(tcontext->connected_peers.size());

            tcontext->download_speed = 0;
            tcontext->upload_speed = 0;
            for (auto const& peer : tcontext->connected_peers) {
                const auto& stats = m_connection_stats->get(peer->connection_id).value_or({});
                pviews.append(PeerView(
                    peer->id,
                    peer->peer->address.ipv4_address().to_deprecated_string(),
                    peer->peer->address.port(),
                    peer->bitfield.progress(),
                    stats.download_speed,
                    stats.upload_speed,
                    stats.bytes_downloaded,
                    stats.bytes_uploaded,
                    peer->we_are_choking_peer,
                    peer->peer_is_choking_us,
                    peer->we_are_interested_in_peer,
                    peer->peer_is_interested_in_us,
                    true));
                tcontext->download_speed += stats.download_speed;
                tcontext->upload_speed += stats.upload_speed;
            }

            views->set(info_hash, TorrentView(info_hash, torrent->display_name(), tcontext->total_length, torrent->state(), tcontext->local_bitfield.progress(), tcontext->download_speed, tcontext->upload_speed, torrent->data_path(), pviews, tcontext->local_bitfield));
        } else {
            views->set(info_hash, TorrentView(info_hash, torrent->display_name(), torrent->meta_info().total_length(), torrent->state(), 0, 0, 0, torrent->data_path(), {}, { (u64)torrent->meta_info().total_length() }));
        }
    }

    return views;
}

ErrorOr<void> Engine::create_file(const AK::DeprecatedString& absolute_path)
{
    // Most of this code is copied from Userland/Utilities/mkdir/mkdir.cpp
    mode_t const default_mode = 0755;

    LexicalPath lexical_path(absolute_path);
    auto& parts = lexical_path.parts_view();
    size_t num_parts = parts.size();

    StringBuilder path_builder;
    path_builder.append('/');

    for (size_t idx = 0; idx < num_parts; ++idx) {
        bool const is_final = (idx == (num_parts - 1));
        path_builder.append(parts[idx]);
        auto const path = path_builder.to_deprecated_string();

        struct stat st;
        if (stat(path.characters(), &st) < 0) {
            if (errno != ENOENT) {
                perror("stat");
                warnln("Error other than 'no such file or directory' with: {}", path);
                return Error::from_string_literal("Error creating directory");
            }

            if (is_final) {
                dbgln("creating file '{}'", path);
                int fd = creat(path.characters(), default_mode);
                if (fd < 0) {
                    perror("creat");
                    warnln("Error creating file '{}'", path);
                    return Error::from_string_literal("Cannot create file");
                }
                close(fd);
                return {};
            }

            dbgln("creating directory '{}'", path);
            if (mkdir(path.characters(), default_mode) < 0) {
                perror("mkdir");
                warnln("Error creating directory '{}'", path);
                return Error::from_string_literal("Cannot create directory");
            }
        } else {
            if (is_final) {
                if (!S_ISREG(st.st_mode)) {
                    warnln("Error: file already exists but isn't a regular file: '{}'", path);
                    return Error::from_string_literal("File alrady exists but isn't a regular file");
                } else {
                    dbgln("file '{}' already exists", path);
                }
            } else if (!S_ISDIR(st.st_mode)) {
                warnln("Cannot create directory, a non-directory file already exists for path '{}'", path);
                return Error::from_string_literal("Cannot create directory");
            } else {
                dbgln("directory '{}' already exists", path);
            }
        }
        path_builder.append('/');
    }
    return {};
}

void Engine::start_torrent(InfoHash info_hash)
{
    deferred_invoke([this, info_hash]() {
        NonnullRefPtr<Torrent> torrent = *m_torrents.get(info_hash).value();
        torrent->set_state(TorrentState::CHECKING);

        for (auto local_file : *torrent->local_files()) {
            auto err = create_file(local_file->local_path());
            if (err.is_error()) {
                dbgln("error creating file: {}", err.error());
                torrent->set_state(TorrentState::ERROR);
                return;
            }
            auto fe = Core::File::open(local_file->local_path(), Core::File::OpenMode::ReadWrite);
            if (fe.is_error()) {
                dbgln("error opening file: {}", fe.error());
                torrent->set_state(TorrentState::ERROR);
                return;
            }
            auto f = fe.release_value();

            auto x = Core::System::posix_fallocate(f->fd(), 0, local_file->meta_info_file()->length());
            if (x.is_error()) {
                dbgln("error posix_fallocating file: {}", x.error());
                torrent->set_state(TorrentState::ERROR);
                return;
            }
            f->close();
        }

        torrent->checking_in_background(m_skip_checking, m_assume_valid, [this, torrent, info_hash, origin_event_loop = &Core::EventLoop::current()](BitField local_bitfield) {
            // Checking finished callback, still on the background thread
            torrent->set_state(TorrentState::STARTED);
            // announcing using the UI thread/loop:
            origin_event_loop->deferred_invoke([this, torrent, info_hash, local_bitfield = move(local_bitfield)] {
                dbgln("we have {}/{} pieces", local_bitfield.ones(), torrent->piece_count());

                auto tcontext = make_ref_counted<TorrentContext>(info_hash,
                    PeerId(torrent->local_peer_id()),
                    (u64)torrent->meta_info().total_length(),
                    (u64)torrent->meta_info().piece_length(),
                    torrent->local_port(),
                    local_bitfield,
                    torrent->data_file_map().release_nonnull());

                for (u64 i = 0; i < tcontext->piece_count; i++) {
                    if (!tcontext->local_bitfield.get(i))
                        tcontext->missing_pieces.set(i, make_ref_counted<PieceStatus>(i));
                }
                m_torrent_contexts.set(tcontext->info_hash, tcontext);

                announce(*torrent, [this, tcontext, info_hash](auto peers) {
                    // announce finished callback, now on the UI loop/thread
                    // TODO: if we seed, we don't add peers.
                    dbgln("Peers ({}) from tracker:", peers.size());
                    for (auto& peer : peers) {
                        dbgln("{}", peer);
                    }
                    if (tcontext->local_bitfield.progress() < 100) {
                        for (auto const& peer_address : peers) {
                            // TODO algo for dupes, deleted (tracker announce doesn't return one anymore and we're downloading/uploading from it?)
                            // assuming peers are added only once
                            tcontext->peers.append(make_ref_counted<Peer>(peer_address, tcontext));
                        }
                        connect_more_peers(*tcontext);
                    }
                }).release_value_but_fixme_should_propagate_errors();
            });
        });
    });
}

void Engine::stop_torrent(InfoHash info_hash)
{
    auto torrent = m_torrents.get(info_hash).value();
    torrent->set_state(TorrentState::STOPPED);
    for (auto& pcontext : m_torrent_contexts.get(info_hash).value()->connected_peers) {
        m_comm.close_connection(pcontext->connection_id, "Stopping torrent");
    }
    m_torrent_contexts.remove(info_hash);
}

void Engine::cancel_checking(InfoHash info_hash)
{
    auto torrent = m_torrents.get(info_hash).value();
    torrent->cancel_checking();
    torrent->set_state(TorrentState::STOPPED);
}

void Engine::register_views_update_callback(int interval_ms, Function<void(NonnullOwnPtr<HashMap<InfoHash, TorrentView>>)> views_updated)
{
    m_event_loop->deferred_invoke([&, interval_ms, views_updated = move(views_updated)] () mutable {
        add<Core::Timer>(interval_ms, [&, views_updated = move(views_updated)] {
            views_updated(torrents_views());
        }).start();
    });
}

ErrorOr<String> Engine::url_encode_bytes(u8 const* bytes, size_t length)
{
    StringBuilder builder;
    for (size_t i = 0; i < length; ++i) {
        builder.appendff("%{:02X}", bytes[i]);
    }
    return builder.to_string();
}

// TODO: don't use torrent as a parameter and use exactly what we need instead. We'll also probably soon require to return more than just the peer list.
ErrorOr<void> Engine::announce(Torrent& torrent, Function<void(Vector<Core::SocketAddress>)> on_complete)
{
    auto info_hash = TRY(url_encode_bytes(torrent.meta_info().info_hash().data(), 20));
    auto my_peer_id = TRY(url_encode_bytes(torrent.local_peer_id().data(), 20));
    auto url = URL(torrent.meta_info().announce());

    // https://www.bittorrent.org/beps/bep_0007.html
    // should be generated per session per torrent.
    u64 key = get_random<u64>();

    url.set_query(TRY(String::formatted("info_hash={}&peer_id={}&port={}&uploaded=1&downloaded=1&left=10&key={}", info_hash, my_peer_id, torrent.local_port(), key)).to_deprecated_string());
    dbgln("query: {}", url.query());
    auto request = m_protocol_client->start_request("GET", url);
    m_active_requests.set(*request);

    request->on_buffered_request_finish = [this, &request = *request, on_complete = move(on_complete)](bool success, auto total_size, auto&, auto status_code, ReadonlyBytes payload) {
        auto err = [&]() -> ErrorOr<void> {
            dbgln("We got back the payload, size: {}, success: {}, status_code: {}", total_size, success, status_code);
            auto response = TRY(BDecoder::parse<Dict>(payload));

            if (response.contains("failure reason")) {
                dbgln("Failure:  {}", response.get_string("failure reason"));
                return {};
            }
            // dbgln("interval: {}", response.get<i64>("interval"));
            Vector<Core::SocketAddress> peers;
            if (response.has<List>("peers")) {
                for (auto peer : response.get<List>("peers")) {
                    auto peer_dict = peer.get<Dict>();
                    auto ip_address = IPv4Address::from_string(peer_dict.get_string("ip"));
                    // TODO: check if ip string is a host name and resolve it.
                    VERIFY(ip_address.has_value());
                    peers.append({ ip_address.value(), static_cast<u16>(peer_dict.get<i64>("port")) });
                }
            } else {
                // https://www.bittorrent.org/beps/bep_0023.html compact peers list
                auto peers_bytes = response.get<ByteBuffer>("peers");
                VERIFY(peers_bytes.size() % 6 == 0);
                auto stream = FixedMemoryStream(peers_bytes.bytes());
                while (!stream.is_eof())
                    peers.append({ TRY(stream.read_value<NetworkOrdered<u32>>()), TRY(stream.read_value<NetworkOrdered<u16>>()) });
            }

            on_complete(move(peers));

            return {};
        }.operator()();
        if (err.is_error()) {
            dbgln("Error announcing: {}", err.error().string_literal());
        }
        deferred_invoke([this, &request]() {
            m_active_requests.remove(request);
        });
    };
    request->set_should_buffer_all_input(true);

    return {};
}

Engine::Engine(NonnullRefPtr<Protocol::RequestClient> protocol_client, bool skip_checking, bool assume_valid)
    : m_protocol_client(protocol_client)
    , m_skip_checking(skip_checking)
    , m_assume_valid(assume_valid)
{
    m_thread = Threading::Thread::construct([this]() -> intptr_t {
        m_event_loop = make<Core::EventLoop>();
        return m_event_loop->exec();
    },
        "Engine"sv);

    m_thread->start();

    m_comm.on_peer_disconnect = [&](ConnectionId connection_id, DeprecatedString reason) {
        m_event_loop->deferred_invoke([&, connection_id, reason] {
            dbgln("Disconnected: {}", reason);
            peer_disconnected(connection_id);
        });
    };

    m_comm.on_message_receive = [&](ConnectionId connection_id, ReadonlyBytes message_bytes) {
        m_event_loop->deferred_invoke([&, connection_id, buffer = MUST(ByteBuffer::copy(message_bytes))] {
            auto err = parse_input_message(connection_id, buffer.bytes());
            if (err.is_error()) {
                m_comm.close_connection(connection_id, DeprecatedString::formatted("Error parsing input message for connection id {}: {}", connection_id, err.error().string_literal()));
            }
        });
    };

    m_comm.on_connection_established = [&](ConnectionId connection_id) {
        m_event_loop->deferred_invoke([&, connection_id] {
            auto maybe_peer = m_connecting_peers.take(connection_id);
            VERIFY(maybe_peer.has_value());

            auto peer = maybe_peer.release_value();
            auto pcontext = make_ref_counted<PeerContext>(peer, connection_id, peer->id_from_handshake.value());
            m_connected_peers.set(connection_id, pcontext);
            peer->torrent_context->connected_peers.set(pcontext);

            dbgln("Peer connected: {}", *peer);
            m_comm.send_message(connection_id, make<BitFieldMessage>(peer->torrent_context->local_bitfield));
        });
    };

    m_comm.on_handshake_from_outgoing_connection = [&](ConnectionId connection_id, HandshakeMessage handshake, auto accept_connection) {
        m_event_loop->deferred_invoke([&, connection_id, handshake, accept_connection = move(accept_connection)] {
            auto maybe_peer = m_connecting_peers.get(connection_id);
            VERIFY(maybe_peer.has_value());
            auto peer = maybe_peer.release_value();

            if (peer->torrent_context->info_hash != handshake.info_hash()) {
                dbgln("Peer sent a handshake with the wrong torrent info hash, disconnecting.");
                accept_connection(false);
                return;
            }

            peer->id_from_handshake = handshake.peer_id();
            accept_connection(true);
        });
    };

    m_comm.on_handshake_from_incoming_connection = [&](ConnectionId connection_id, HandshakeMessage handshake, Core::SocketAddress address, auto accept_connection) {
        m_event_loop->deferred_invoke([&, connection_id, handshake, address, accept_connection = move(accept_connection)] {
            VERIFY(!m_connecting_peers.contains(connection_id));
            VERIFY(!m_connected_peers.contains(connection_id));

            auto maybe_tcontext = m_torrent_contexts.get(handshake.info_hash());
            if (maybe_tcontext.has_value()) {
                // TODO: Add more checks before accepting the connection.
                auto tcontext = maybe_tcontext.release_value();
                if (tcontext->local_peer_id == handshake.peer_id()) {
                    dbgln("Refusing connection from ourselves.");
                    accept_connection({});
                    return;
                }

                // FIXME: The peer likely already exists in tcontext->peers
                auto peer = make_ref_counted<Peer>(address, *tcontext);
                peer->status = PeerStatus::InUse;
                peer->id_from_handshake = handshake.peer_id();
                m_connecting_peers.set(connection_id, peer);

                accept_connection(HandshakeMessage(tcontext->info_hash, tcontext->local_peer_id));
            } else {
                dbgln("Peer sent a handshake with an unknown torrent info hash, disconnecting.");
                accept_connection({});
            }
        });
    };

    m_comm.on_connection_stats_update = [&](NonnullOwnPtr<HashMap<ConnectionId, ConnectionStats>> stats) {
        m_event_loop->deferred_invoke([&, stats = move(stats)]() mutable {
            m_connection_stats = move(stats);
        });
    };
}

Engine::~Engine()
{
    dbgln("Engine::~Engine()");
    //    for (auto torrent : m_torrents) {
    //        torrent->cancel_checking();
    //    }
    //    m_torrents.clear();
}
ErrorOr<NonnullRefPtr<Engine>> Engine::try_create(bool skip_checking, bool assume_valid)
{
    auto protocol_client = TRY(Protocol::RequestClient::try_create());
    return adopt_nonnull_ref_or_enomem(new (nothrow) Engine(move(protocol_client), skip_checking, assume_valid));
}

void Engine::connect_more_peers(NonnullRefPtr<TorrentContext> torrent)
{
    u64 total_connections = 0;
    for (auto const& [_, tcontext] : m_torrent_contexts) {
        total_connections += tcontext->connected_peers.size();
    }
    u64 total_connections_for_torrent = torrent->connected_peers.size();
    for (auto const& [_, peer] : m_connecting_peers) {
        if (peer->torrent_context == torrent)
            total_connections_for_torrent++;
    }

    size_t available_slots = min(max_connections_per_torrent - total_connections_for_torrent, max_total_connections - total_connections);
    dbgln("We have {} available slots for new connections", available_slots);

    auto peer_it = torrent->peers.begin();
    while (available_slots > 0 && !peer_it.is_end()) {
        auto& peer = *peer_it;
        if (peer->status == PeerStatus::Available) {
            auto maybe_conn_id = m_comm.connect(peer->address, HandshakeMessage(torrent->info_hash, torrent->local_peer_id));
            if (maybe_conn_id.is_error()) {
                dbgln("Failed to create a connection for peer {}, error: {}", peer->address, maybe_conn_id.error());
                peer->status = PeerStatus::Errored;
            } else {
                peer->status = PeerStatus::InUse;
                dbgln("Connecting to peer {} connection id: {}", peer->address, maybe_conn_id.value());
                m_connecting_peers.set(maybe_conn_id.value(), peer);
                available_slots--;
            }
        }
        peer_it++;
    }
}

u64 Engine::get_available_peers_count(NonnullRefPtr<TorrentContext> torrent) const
{
    u64 count = 0;
    for (auto const& peer : torrent->peers) {
        if (peer->status == PeerStatus::Available)
            count++;
    }
    return count;
}

void Engine::peer_disconnected(ConnectionId connection_id)
{
    RefPtr<Peer> peer;
    if (m_connecting_peers.contains(connection_id)) {
        peer = m_connecting_peers.take(connection_id).release_value();
    } else {
        auto peer_context = m_connected_peers.take(connection_id).release_value();
        peer = peer_context->peer;

        auto torrent = peer->torrent_context;
        for (auto const& piece_index : peer_context->interesting_pieces) {
            torrent->missing_pieces.get(piece_index).value()->havers.remove(peer_context);
        }

        auto& piece = peer_context->incoming_piece;
        if (piece.index.has_value()) {
            insert_piece_in_heap(torrent, piece.index.value());
            piece.index = {};
        }

        torrent->connected_peers.remove(peer_context);
    }

    peer->status = PeerStatus::Errored;

    // FIXME: add another condition to check if the torrent status is still downloading
    if (peer->torrent_context->local_bitfield.progress() < 100)
        connect_more_peers(peer->torrent_context);
}

ErrorOr<void> Engine::piece_downloaded(u64 index, ReadonlyBytes data, NonnullRefPtr<PeerContext> peer)
{
    auto torrent = peer->peer->torrent_context;
    if (TRY(torrent->data_file_map->validate_hash(index, data))) {
        TRY(torrent->data_file_map->write_piece(index, data));

        torrent->local_bitfield.set(index, true);
        auto& havers = torrent->missing_pieces.get(index).value()->havers;
        dbgln("Havers of that piece {} we just downloaded: {}", index, havers.size());
        for (auto& haver : havers) {
            VERIFY(haver->interesting_pieces.remove(index));
            dbgln("Removed piece {} from interesting pieces of {}", index, haver);
            if (haver->interesting_pieces.is_empty()) {
                dbgln("Peer {} has no more interesting pieces, sending a NotInterested message", haver);
                m_comm.send_message(haver->connection_id, make<NotInterestedMessage>());
                haver->we_are_interested_in_peer = false;
            }
        }
        torrent->missing_pieces.remove(index);

        dbgln("We completed piece {}", index);

        for (auto const& connected_peer : torrent->connected_peers) {
            m_comm.send_message(connected_peer->connection_id, make<HaveMessage>(index));
        }

        if (torrent->local_bitfield.progress() == 100) {
            dbgln("Torrent download completed: {}", torrent->info_hash);
            VERIFY(torrent->piece_heap.is_empty());
            VERIFY(torrent->missing_pieces.is_empty());

            for (auto const& connected_peer : torrent->connected_peers) {
                if (connected_peer->bitfield.progress() == 100)
                    m_comm.close_connection(connected_peer->connection_id, "Torrent fully downloaded.");
            }

            m_torrents.get(torrent->info_hash).value()->set_state(TorrentState::SEEDING);
            return {};
        }
    } else {
        insert_piece_in_heap(torrent, index);
        dbgln("Piece {} failed hash check", index);
    }
    TRY(piece_or_peer_availability_updated(torrent));
    return {};
}

ErrorOr<void> Engine::piece_or_peer_availability_updated(NonnullRefPtr<TorrentContext> torrent)
{
    size_t available_slots = 0;
    for (auto const& peer : torrent->connected_peers)
        available_slots += !peer->active;

    dbgln("We have {} inactive peers out of {} connected peers.", available_slots, torrent->connected_peers.size());
    for (size_t i = 0; i < available_slots; i++) {
        dbgln("Trying to start a piece download on a {}th peer", i);
        if (torrent->piece_heap.is_empty())
            return {};

        // TODO find out the rarest available piece, because the rarest piece might not be available right now.
        auto next_piece_index = torrent->piece_heap.peek_min()->index_in_torrent;
        dbgln("Picked next piece for download {}", next_piece_index);
        // TODO improve how we select the peer. Choking algo, bandwidth, etc
        bool found_peer = false;
        for (auto& haver : torrent->missing_pieces.get(next_piece_index).value()->havers) {
            dbgln("Peer {} is choking us: {}, is active: {}", haver, haver->peer_is_choking_us, haver->active);
            if (!haver->peer_is_choking_us && !haver->active) {
                dbgln("Requesting piece {} from peer {}", next_piece_index, haver);
                haver->active = true;
                u32 block_length = min(BlockLength, torrent->piece_length(next_piece_index));
                m_comm.send_message(haver->connection_id, make<RequestMessage>(next_piece_index, 0, block_length));

                found_peer = true;
                break;
            }
        }
        if (found_peer) {
            dbgln("Found peer for piece {}, popping the piece from the heap", next_piece_index);
            auto piece_status = torrent->piece_heap.pop_min();
            piece_status->currently_downloading = true;
            VERIFY(piece_status->index_in_torrent == next_piece_index);
        } else {
            dbgln("No more available peer to download piece {}", next_piece_index);
            break;
        }
    }
    return {};
}

ErrorOr<void> Engine::peer_has_piece(u64 piece_index, NonnullRefPtr<PeerContext> peer)
{
    auto& torrent = peer->peer->torrent_context;
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

void Engine::insert_piece_in_heap(NonnullRefPtr<TorrentContext> torrent, u64 piece_index)
{
    auto piece_status = torrent->missing_pieces.get(piece_index).value();
    piece_status->currently_downloading = false;
    torrent->piece_heap.insert(*piece_status);
}

ErrorOr<void> Engine::parse_input_message(ConnectionId connection_id, ReadonlyBytes message_bytes)
{
    NonnullRefPtr<PeerContext> peer = *m_connected_peers.get(connection_id).value();
    auto stream = FixedMemoryStream(message_bytes);

    using MessageType = Bits::Message::Type;
    auto message_type = TRY(stream.read_value<MessageType>());

    dbgln("Got message type {}", Bits::Message::to_string(message_type));

    switch (message_type) {
    case MessageType::Choke:
        peer->peer_is_choking_us = true;
        TRY(piece_or_peer_availability_updated(peer->peer->torrent_context));
        break;
    case MessageType::Unchoke:
        peer->peer_is_choking_us = false;
        TRY(piece_or_peer_availability_updated(peer->peer->torrent_context));
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
        TRY(handle_bitfield(make<BitFieldMessage>(stream, peer->peer->torrent_context->piece_count), peer));
        break;
    case MessageType::Request:
        TRY(handle_request(make<RequestMessage>(stream), peer));
        break;
    case MessageType::Piece:
        TRY(handle_piece(make<PieceMessage>(stream), peer));
        break;
    case MessageType::Cancel:
        dbgln("ERROR: message type Cancel is unsupported");
        break;
    default:
        dbgln("ERROR: Got unsupported message type: {:02X}: {}", (u8)message_type, Message::to_string(message_type));
        break;
    }
    return {};
}

ErrorOr<void> Engine::handle_bitfield(NonnullOwnPtr<BitFieldMessage> bitfield, NonnullRefPtr<PeerContext> peer)
{
    peer->bitfield = bitfield->bitfield;
    dbgln("Receiving BitField from peer: {}", peer->bitfield);

    bool interesting = false;
    auto torrent = peer->peer->torrent_context;
    for (auto& missing_piece : torrent->missing_pieces.keys()) {
        if (peer->bitfield.get(missing_piece)) {
            interesting = true;
            TRY(peer_has_piece(missing_piece, peer));
        }
    }

    VERIFY(!peer->we_are_interested_in_peer);

    if (interesting) {
        // TODO we need a (un)choking algo
        m_comm.send_message(peer->connection_id, make<UnchokeMessage>());
        peer->we_are_choking_peer = false;

        m_comm.send_message(peer->connection_id, make<InterestedMessage>());
        peer->we_are_interested_in_peer = true;

        TRY(piece_or_peer_availability_updated(torrent));
    } else {
        if (get_available_peers_count(torrent) > 0) {
            // TODO: set error type so we can connect to it again later if we need to
            // TODO: we have no idea if other peers will be reacheable or have better piece availability.
            m_comm.close_connection(peer->connection_id, "Peer has no interesting pieces, and other peers are out there, disconnecting.");
        } else {
            dbgln("Peer has no interesting pieces, but we have no other peers to connect to. Staying connected in the hope that it will get some interesting pieces.");
        }
    }

    return {};
}

ErrorOr<void> Engine::handle_have(NonnullOwnPtr<HaveMessage> have_message, NonnullRefPtr<PeerContext> peer)
{
    auto piece_index = have_message->piece_index;
    dbgln("Peer has piece {}, setting in peer bitfield, bitfield size: {}", piece_index, peer->bitfield.size());
    peer->bitfield.set(piece_index, true);

    if (peer->peer->torrent_context->missing_pieces.contains(piece_index)) {
        TRY(peer_has_piece(piece_index, peer));
        if (!peer->we_are_interested_in_peer) {
            m_comm.send_message(peer->connection_id, make<UnchokeMessage>());
            peer->we_are_choking_peer = false;

            m_comm.send_message(peer->connection_id, make<InterestedMessage>());
            peer->we_are_interested_in_peer = true;
        }
        TRY(piece_or_peer_availability_updated(peer->peer->torrent_context));
    } else {
        if (peer->bitfield.progress() == 100 && peer->peer->torrent_context->local_bitfield.progress() == 100) {
            m_comm.close_connection(peer->connection_id, "Peer and us have all pieces, disconnecting");
        }
    }

    return {};
}

ErrorOr<void> Engine::handle_interested(NonnullRefPtr<Bits::PeerContext> peer)
{
    peer->peer_is_interested_in_us = true;
    peer->we_are_choking_peer = false;
    m_comm.send_message(peer->connection_id, make<UnchokeMessage>());
    return {};
}

ErrorOr<void> Engine::handle_piece(NonnullOwnPtr<PieceMessage> piece_message, NonnullRefPtr<PeerContext> peer)
{
    auto torrent = peer->peer->torrent_context;
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
        deferred_invoke([this, index, peer, bytes = piece.data.bytes()] {
            MUST(piece_downloaded(index, bytes, peer)); // TODO run disk IO related stuff like that in a separate thread for potential performance gains
        });
        piece.index = {};
        peer->active = false;
    } else {
        if (peer->peer_is_choking_us) {
            dbgln("Weren't done downloading the blocks for this piece {}, but peer is choking us, so we're giving up on it", index);
            piece.index = {};
            peer->active = false;
            insert_piece_in_heap(torrent, index);
            TRY(piece_or_peer_availability_updated(torrent));
        } else {
            auto next_block_length = min((size_t)BlockLength, (size_t)piece.length - piece.offset);
            m_comm.send_message(peer->connection_id, make<RequestMessage>(index, piece.offset, next_block_length));
        }
    }
    return {};
}

ErrorOr<void> Engine::handle_request(NonnullOwnPtr<RequestMessage> request, NonnullRefPtr<PeerContext> peer)
{
    // TODO: validate request parameters, disconnect peer if they're invalid.
    auto torrent = peer->peer->torrent_context;
    auto piece = TRY(ByteBuffer::create_uninitialized(torrent->piece_length(request->piece_index)));
    TRY(torrent->data_file_map->read_piece(request->piece_index, piece));

    m_comm.send_message(peer->connection_id, make<PieceMessage>(request->piece_index, request->piece_offset, TRY(piece.slice(request->piece_offset, request->block_length))));
    return {};
}

}