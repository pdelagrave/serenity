/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "Torrent.h"
#include "Data/Comm.h"
#include <LibCore/Object.h>
#include <LibProtocol/RequestClient.h>
namespace Bits {

class Engine : public Core::Object {
    C_OBJECT(Engine);

public:
    static ErrorOr<NonnullRefPtr<Engine>> try_create(bool skip_checking, bool assume_valid);
    ~Engine();

    Vector<NonnullRefPtr<Torrent>> torrents() { return m_torrents; }
    void add_torrent(NonnullOwnPtr<MetaInfo>, DeprecatedString);
    void start_torrent(int);
    void stop_torrent(int);
    void cancel_checking(int);

protected:
    virtual void timer_event(Core::TimerEvent&) override;

private:
    Engine(NonnullRefPtr<Protocol::RequestClient>, bool skip_checking, bool assume_valid);
    static ErrorOr<String> url_encode_bytes(u8 const* bytes, size_t length);
    static ErrorOr<String> hexdump(ReadonlyBytes);
    static ErrorOr<void> create_file(DeprecatedString const& path);

    NonnullRefPtr<Protocol::RequestClient> m_protocol_client;
    HashTable<NonnullRefPtr<Protocol::Request>> m_active_requests;

    Vector<NonnullRefPtr<Torrent>> m_torrents;
    ErrorOr<void> announce(Torrent&, Function<void()> on_complete);

    Data::Comm comm;
    bool m_skip_checking;
    bool m_assume_valid;
};

}