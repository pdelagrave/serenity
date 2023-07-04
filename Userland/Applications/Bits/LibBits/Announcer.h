/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "FixedSizeByteString.h"
#include <LibCore/Object.h>
#include <LibProtocol/RequestClient.h>

namespace Bits {

struct AnnounceStats {
    u64 downloaded { 0 };
    u64 uploaded { 0 };
    u64 left { 0 };
};

class Announcer : public Core::Object {
    C_OBJECT(Announcer)
public:
    static ErrorOr<NonnullRefPtr<Announcer>> create(InfoHash info_hash, AK::URL announce_url, PeerId local_peer_id, u16 listen_port, u64 torrent_session_key, Function<AnnounceStats()> get_stats_for_announce, Function<void(Vector<Core::SocketAddress>)> on_success);
    void completed();
    void stopped();
protected:
    virtual void timer_event(Core::TimerEvent& event) override;

private:
    enum class EventType {
        Started,
        Completed,
        Stopped,
        None
    };

    Announcer(NonnullRefPtr<Protocol::RequestClient> http_client, InfoHash info_hash, AK::URL announce_url, PeerId local_peer_id, u16 listen_port, u64 torrent_session_key, Function<AnnounceStats()> get_stats_for_announce, Function<void(Vector<Core::SocketAddress>)> on_success);
    ErrorOr<void> announce(EventType event_type = EventType::None);
    static DeprecatedString url_encode_bytes(ReadonlyBytes bytes);

    NonnullRefPtr<Protocol::RequestClient> m_http_client;
    const InfoHash m_info_hash;
    const AK::URL m_announce_url;
    const PeerId m_local_peer_id;
    const u16 m_listen_port;
    const u64 m_torrent_session_key;
    const Function<AnnounceStats()> m_get_stats_for_announce;
    const Function<void(Vector<Core::SocketAddress>)> m_on_success;

    HashTable<NonnullRefPtr<Protocol::Request>> m_active_requests;
    int m_interval = 60 * 1000;
};

}
