/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../FixedSizeByteString.h"
#include "Connection.h"
#include "HandshakeMessage.h"
#include <AK/Stack.h>
#include <LibCore/EventLoop.h>
#include <LibCore/TCPServer.h>
#include <LibThreading/Mutex.h>
#include <LibThreading/Thread.h>

namespace Bits {

class Message;

// FIXME: move bandwidth/speed management to the engine, Comm should only report how many bytes were downloaded/uploaded since the last stats callback invocation.
struct ConnectionStats {
    ConnectionId connection_id;
    u64 bytes_downloaded { 0 };
    u64 bytes_uploaded { 0 };
    u64 download_speed { 0 };
    u64 upload_speed { 0 };
};

class Comm : public Core::Object {
    C_OBJECT(Comm);

public:
    Comm(u16 const listen_port);
    void close_connection(ConnectionId connection_id, DeprecatedString reason);

    Function<void(ConnectionId, DeprecatedString)> on_peer_disconnect;
    Function<void(ConnectionId, ReadonlyBytes)> on_message_receive;
    Function<void(ConnectionId)> on_connection_established;
    Function<void(ConnectionId, HandshakeMessage, Function<void(bool)>)> on_handshake_from_outgoing_connection;
    Function<void(ConnectionId, HandshakeMessage, Core::SocketAddress, Function<void(Optional<HandshakeMessage>)>)> on_handshake_from_incoming_connection;
    Function<void(NonnullOwnPtr<HashMap<ConnectionId, ConnectionStats>>)> on_connection_stats_update;

    ConnectionId connect(Core::SocketAddress address, HandshakeMessage handshake);
    void send_message(ConnectionId connection_id, NonnullOwnPtr<Message> message);

protected:
    void timer_event(Core::TimerEvent&) override;

private:
    OwnPtr<Core::EventLoop> m_event_loop;
    RefPtr<Threading::Thread> m_thread;
    NonnullRefPtr<Core::TCPServer> m_server;

    timeval m_last_speed_measurement;
    HashMap<ConnectionId, ConnectionStats> m_connection_stats;

    // Comm BT low level network functions
    HashMap<ConnectionId, NonnullRefPtr<Connection>> m_connections;

    ErrorOr<void> read_from_socket(NonnullRefPtr<Connection> connection);
    ErrorOr<void> flush_output_buffer(NonnullRefPtr<Connection> connection);

    ErrorOr<void> send_handshake(HandshakeMessage handshake, NonnullRefPtr<Connection> connection);
    void close_connection_internal(NonnullRefPtr<Connection> connection, DeprecatedString error_message);

    ErrorOr<void> on_ready_to_accept();
    ErrorOr<NonnullRefPtr<Connection>> create_connection(ConnectionId connection_id, NonnullOwnPtr<Core::TCPSocket> socket);
};

}
