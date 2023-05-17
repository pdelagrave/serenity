/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Engine.h"
#include <AK/Format.h>
#include <LibCore/EventLoop.h>
#include <LibProtocol/Request.h>

namespace Bits {

int Engine::start()
{
    m_event_loop = make<Core::EventLoop>();
    return m_event_loop->exec();
}

void Engine::custom_event(Core::CustomEvent& event)
{
    auto maybe_error = [&]() -> ErrorOr<void> {
        if (is<CommandEvent>(event)) {
            auto command = reinterpret_cast<CommandEvent&>(event).command();
            if (is<AddTorrent>(*command)) {
                auto& add_torrent = reinterpret_cast<AddTorrent&>(*command);
                m_torrents.append(adopt_nonnull_ref_or_enomem(new (nothrow) Torrent(add_torrent.meta_info(), add_torrent.data_path())).release_value());
            } else if (is<StartTorrent>(*command)) {
                auto& start_torrent = reinterpret_cast<StartTorrent&>(*command);
                NonnullRefPtr<Torrent>& torrent = m_torrents.at(start_torrent.torrent_id());
                torrent->set_state(TorrentState::STARTED);
                TRY(announce(torrent));
            } else if (is<StopTorrent>(*command)) {
                auto& stop_torrent = reinterpret_cast<StopTorrent&>(*command);
                m_torrents.at(stop_torrent.torrent_id())->set_state(TorrentState::STOPPED);
            }
        }
        return {};
    }.operator()();
    if (maybe_error.is_error()) {
        dbgln("Error executing command: {}", maybe_error.error().string_literal());
    }
}
void Engine::post(NonnullOwnPtr<Command> command)
{
    m_event_loop->post_event(*this, make<CommandEvent>(move(command)));
}

ErrorOr<String> Engine::url_encode_bytes(u8 const* bytes, size_t length)
{
    StringBuilder builder;
    for (size_t i = 0; i < length; ++i) {
        builder.appendff("%{:02X}", bytes[i]);
    }
    return builder.to_string();
}

ErrorOr<void> Engine::announce(Torrent& torrent)
{
    auto info_hash = TRY(url_encode_bytes(torrent.meta_info().info_hash(), 20));
    u8 m_local_peer_id_bytes[20];
    memcpy(&m_local_peer_id_bytes, "\x57\x39\x6b\x5d\x72\xb4\x7f\x3a\x4b\x26\xcf\x84\xbf\x6b\x93\x52\x3f\x14\xf8\xca", 20);
    auto my_peer_id = TRY(url_encode_bytes(m_local_peer_id_bytes, 20));
    auto my_port = 27007;
    auto url = URL(torrent.meta_info().announce());

    // https://www.bittorrent.org/beps/bep_0007.html
    // should be generated per session per torrent.
    u64 key = get_random<u64>();

    url.set_query(TRY(String::formatted("info_hash={}&peer_id={}&port={}&compact=0&uploaded=1&downloaded=1&left=2&key={}", info_hash, my_peer_id, my_port, key)).to_deprecated_string());
    auto r = m_protocol_client->start_request("GET", url);
    r->on_buffered_request_finish = [&torrent, &my_peer_id](bool success, auto total_size, auto&, auto status_code, ReadonlyBytes payload) {
        auto err = [&]() -> ErrorOr<void> {
            dbgln("We got back the payload, size: {}, success: {}, status_code: {}, torrent state: {}", total_size, success, status_code, state_to_string(torrent.state()));
            auto response = TRY(BDecoder::parse<Dict>(payload));
            dbgln("keys {}", response.keys().size());

            if (response.contains("failure reason")) {
                dbgln("Failure:  {}", response.get_string("failure reason"));
                return {};
            }
            dbgln("interval: {}", response.get<i64>("interval"));
            auto peers = response.get<List>("peers");
            dbgln("peer len: {}", peers.size());

            for (auto peer : peers) {
                auto peer_dict = peer.get<Dict>();
                // TODO: check if ip string is a host name and resolve it.
                Peer p = Peer(peer_dict.get<ByteBuffer>("peer id"), IPv4Address::from_string(peer_dict.get_string("ip")).value(), peer_dict.get<i64>("port"));
                torrent.peers().append(p);
                dbgln("peer: {}  ip: {}, port: {}", hexdump(p.get_id()).release_value(), p.get_address().to_string().release_value(), p.get_port());
            }

            return {};
        }.operator()();
        if (err.is_error()) {
            dbgln("Error announcing: {}", err.error().string_literal());
        }
    };
    r->set_should_buffer_all_input(true);

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

Engine::Engine(NonnullRefPtr<Protocol::RequestClient> protocol_client)
    : m_protocol_client(protocol_client)
{
}

ErrorOr<NonnullRefPtr<Engine>> Engine::try_create()
{
    auto protocol_client = TRY(Protocol::RequestClient::try_create());
    return adopt_nonnull_ref_or_enomem(new (nothrow) Engine(move(protocol_client)));
}

}