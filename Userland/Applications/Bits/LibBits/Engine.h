/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "FixedSizeByteString.h"
#include "Torrent.h"
#include "TorrentView.h"
#include "Userland/Applications/Bits/LibBits/Net/Comm.h"
#include "Userland/Libraries/LibCore/Object.h"
#include "Userland/Libraries/LibProtocol/RequestClient.h"

namespace Bits {

class Engine : public Core::Object {
    C_OBJECT(Engine);

public:
    static ErrorOr<NonnullRefPtr<Engine>> try_create(bool skip_checking, bool assume_valid);
    ~Engine();

    void add_torrent(NonnullOwnPtr<MetaInfo>, DeprecatedString);
    HashMap<InfoHash, TorrentView> torrents();
    void start_torrent(InfoHash info_hash);
    void stop_torrent(InfoHash);
    void cancel_checking(InfoHash);

private:
    Engine(NonnullRefPtr<Protocol::RequestClient>, bool skip_checking, bool assume_valid);
    static ErrorOr<String> url_encode_bytes(u8 const* bytes, size_t length);
    static ErrorOr<void> create_file(DeprecatedString const& path);

    NonnullRefPtr<Protocol::RequestClient> m_protocol_client;
    HashTable<NonnullRefPtr<Protocol::Request>> m_active_requests;

    HashMap<InfoHash, NonnullRefPtr<Torrent>> m_torrents;
    ErrorOr<void> announce(Torrent& torrent, Function<void(Vector<Core::SocketAddress>)> on_complete);

    Comm comm;
    bool m_skip_checking;
    bool m_assume_valid;
};

}