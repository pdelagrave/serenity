/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Engine.h"
#include <AK/LexicalPath.h>
#include <LibCore/System.h>
#include <LibFileSystem/FileSystem.h>
#include <LibProtocol/Request.h>

namespace Bits {

void Engine::add_torrent(NonnullOwnPtr<MetaInfo> meta_info, DeprecatedString data_path)
{
    deferred_invoke([this, meta_info = move(meta_info), data_path]() mutable {
        auto torrent_root_dir = meta_info->root_dir_name();
        DeprecatedString optional_root_dir = "";
        if (torrent_root_dir.has_value()) {
            optional_root_dir = DeprecatedString::formatted("/{}", torrent_root_dir.value());
        }

        auto local_files = make<Vector<NonnullRefPtr<LocalFile>>>();
        for (auto f : meta_info->files()) {
            auto local_path = DeprecatedString::formatted("{}{}/{}", data_path, optional_root_dir, f->path());
            local_files->append(make_ref_counted<LocalFile>(move(local_path), move(f)));
        }

        NonnullRefPtr<Torrent> const& torrent = make_ref_counted<Torrent>(move(meta_info), move(local_files));
        m_torrents.append(move(torrent));
    });
}

void Engine::timer_event(Core::TimerEvent&)
{
    // state is DOWNLOADING?
    // schedule more pieces to download.
    // if we don't have enough peers to schedule more pieces for download, connect to more peers, get their bitfield and this will run again and maybe use them.
    //

    // todo use a priorityqueue to store the next piece to download, fill and update that queue based on the bitfield of every peer, the pop operation will return the rarest piece.
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

void Engine::start_torrent(int torrent_id)
{
    deferred_invoke([this, torrent_id]() {
        auto torrent = m_torrents.at(torrent_id);
        // 1. check if files are there, if not create them
        // 2. read the files and update the bytes already downloaded, how much data we have, so we know if we should start connecting to peers to download the rest.

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

        torrent->checking_in_background(m_skip_checking, m_assume_valid, [this, torrent = move(torrent), origin_event_loop = &Core::EventLoop::current()] {
            // Checking finished callback, still on the background thread
            torrent->set_state(TorrentState::STARTED);
            // announcing using the UI thread/loop:
            origin_event_loop->deferred_invoke([this, torrent = move(torrent)] {
                dbgln("we have {}/{} pieces", torrent->local_bitfield().ones(), torrent->piece_count());
                announce(torrent, [this, torrent = move(torrent)] {
                    // announce finished callback, now on the UI loop/thread
                    int max_peers = 10;
                    int peers_to_add = min(max_peers, torrent->peers().size());
                    dbgln("Total peers {}, adding {}:", torrent->peers().size(), peers_to_add);
                    for (int i = 0; i < peers_to_add; i++) {
                        comm.add_connection(torrent->peers()[i], move(torrent)).release_value_but_fixme_should_propagate_errors();
                    }
                }).release_value_but_fixme_should_propagate_errors();
            });
        });
    });
}

void Engine::stop_torrent(int torrent_id)
{
    dbgln("stop_torrent({})", torrent_id);
    // todo: support stopping the torrent transfer and also stopping the checking if that's the current state.
    TODO();
}

void Engine::cancel_checking(int torrent_id)
{
    auto torrent = m_torrents.at(torrent_id);
    torrent->cancel_checking();
    torrent->set_state(TorrentState::STOPPED);
}

ErrorOr<String> Engine::url_encode_bytes(u8 const* bytes, size_t length)
{
    StringBuilder builder;
    for (size_t i = 0; i < length; ++i) {
        builder.appendff("%{:02X}", bytes[i]);
    }
    return builder.to_string();
}

ErrorOr<void> Engine::announce(Torrent& torrent, Function<void()> on_complete)
{
    auto info_hash = TRY(url_encode_bytes(torrent.meta_info().info_hash().data(), 20));
    auto my_peer_id = TRY(url_encode_bytes(torrent.local_peer_id().data(), 20));
    auto url = URL(torrent.meta_info().announce());

    // https://www.bittorrent.org/beps/bep_0007.html
    // should be generated per session per torrent.
    u64 key = get_random<u64>();

    url.set_query(TRY(String::formatted("info_hash={}&peer_id={}&port={}&uploaded=1&downloaded=1&left=2&key={}", info_hash, my_peer_id, torrent.local_port(), key)).to_deprecated_string());
    dbgln("query: {}", url.query());
    auto request = m_protocol_client->start_request("GET", url);
    m_active_requests.set(*request);

    request->on_buffered_request_finish = [this, &torrent, &request = *request, on_complete = move(on_complete)](bool success, auto total_size, auto&, auto status_code, ReadonlyBytes payload) {
        auto err = [&]() -> ErrorOr<void> {
            dbgln("We got back the payload, size: {}, success: {}, status_code: {}, torrent state: {}", total_size, success, status_code, state_to_string(torrent.state()));
            auto response = TRY(BDecoder::parse<Dict>(payload));

            if (response.contains("failure reason")) {
                dbgln("Failure:  {}", response.get_string("failure reason"));
                return {};
            }
            //dbgln("interval: {}", response.get<i64>("interval"));
            if (response.has<List>("peers")) {
                for (auto peer : response.get<List>("peers")) {
                    auto peer_dict = peer.get<Dict>();
                    Optional<IPv4Address> const& ip_address = IPv4Address::from_string(peer_dict.get_string("ip"));
                    // TODO: check if ip string is a host name and resolve it.
                    VERIFY(ip_address.has_value());
                    auto p = make_ref_counted<Peer>(ip_address.value(), peer_dict.get<i64>("port"));
                    torrent.peers().append(p);
                }
            } else {
                // https://www.bittorrent.org/beps/bep_0023.html "compact" peers list
                auto peers_bytes = response.get<ByteBuffer>("peers");
                VERIFY(peers_bytes.size() % 6 == 0);
                auto stream = FixedMemoryStream(peers_bytes.bytes());
                while (!stream.is_eof()) {
                    auto p = make_ref_counted<Peer>(TRY(stream.read_value<NetworkOrdered<u32>>()), TRY(stream.read_value<NetworkOrdered<u16>>()));
                    torrent.peers().append(p);
                }
            }

            on_complete();

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

ErrorOr<String> Engine::hexdump(ReadonlyBytes bytes)
{
    StringBuilder builder;
    for (size_t i = 0; i < bytes.size(); ++i) {
        builder.appendff("{:02X}", bytes[i]);
    }
    return builder.to_string();
}

Engine::Engine(NonnullRefPtr<Protocol::RequestClient> protocol_client, bool skip_checking, bool assume_valid)
    : m_protocol_client(protocol_client)
    , m_skip_checking(skip_checking)
    , m_assume_valid(assume_valid)
{
}

Engine::~Engine()
{
    dbgln("Engine::~Engine()");
    for (auto torrent : m_torrents) {
        torrent->cancel_checking();
    }
    m_torrents.clear();
}

ErrorOr<NonnullRefPtr<Engine>> Engine::try_create(bool skip_checking, bool assume_valid)
{
    auto protocol_client = TRY(Protocol::RequestClient::try_create());
    return adopt_nonnull_ref_or_enomem(new (nothrow) Engine(move(protocol_client), skip_checking, assume_valid));
}

}