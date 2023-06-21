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

HashMap<InfoHash, TorrentView> Engine::torrents()
{
    auto views = comm.state_snapshot();
    for (auto const& [info_hash, torrent] : m_torrents) {
        auto maybe_view = views.get(info_hash);
        if (maybe_view.has_value()) {
            auto& tview = maybe_view.release_value();
            tview.display_name = torrent->display_name();
            tview.state = torrent->state();
            tview.save_path = torrent->data_path();
        } else {
            views.set(info_hash, TorrentView(info_hash, torrent->display_name(), torrent->meta_info().total_length(), torrent->state(), 0, 0, 0, torrent->data_path(), {}, {(u64)torrent->meta_info().total_length()}));
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
        auto torrent = m_torrents.get(info_hash).value();
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
                comm.activate_torrent(tcontext);

                announce(*torrent, [this, tcontext, info_hash](auto peers) {
                    // announce finished callback, now on the UI loop/thread
                    // TODO: if we seed, we don't add peers.
                    dbgln("Peers from tracker:");
                    for (auto& peer : peers) {
                        dbgln("{}", peer);
                    }
                    if (tcontext->local_bitfield.progress() < 100)
                        comm.add_peers(info_hash, move(peers));
                }).release_value_but_fixme_should_propagate_errors();
            });
        });
    });
}

void Engine::stop_torrent(InfoHash info_hash)
{
    auto torrent = m_torrents.get(info_hash).value();
    torrent->set_state(TorrentState::STOPPED);
    comm.deactivate_torrent(info_hash);
}

void Engine::cancel_checking(InfoHash info_hash)
{
    auto torrent = m_torrents.get(info_hash).value();
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
    comm.on_torrent_download_completed = [this](auto info_hash) {
        dbgln("Torrent download completed: {}", info_hash);
        auto torrent = m_torrents.get(info_hash).value();
        torrent->set_state(TorrentState::SEEDING);
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

}