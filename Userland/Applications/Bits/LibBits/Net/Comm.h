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

class Comm : public Core::Object {
    C_OBJECT(Comm);

public:
    Comm();
    void close_connection(ConnectionId connection_id, DeprecatedString reason);

    Function<void(ConnectionId, DeprecatedString)> on_peer_disconnect;
    Function<void(ConnectionId, ReadonlyBytes)> on_message_receive;
    Function<void(ConnectionId)> on_connection_established;
    Function<bool(ConnectionId, HandshakeMessage)> on_handshake_from_outgoing_connection;
    Function<Optional<HandshakeMessage>(ConnectionId, HandshakeMessage, Core::SocketAddress)> on_handshake_from_incoming_connection;
    ErrorOr<ConnectionId> connect(Core::SocketAddress address, HandshakeMessage handshake);
    void send_message(ConnectionId connection_id, NonnullOwnPtr<Message> message);

protected:
    void timer_event(Core::TimerEvent&) override;

private:
    OwnPtr<Core::EventLoop> m_event_loop;
    RefPtr<Threading::Thread> m_thread;
    NonnullRefPtr<Core::TCPServer> m_server;

    timeval m_last_speed_measurement;

    // Comm BT low level network functions
    HashMap<ConnectionId, NonnullRefPtr<Connection>> m_connections;

    ErrorOr<void> read_from_socket(NonnullRefPtr<Connection> connection);
    ErrorOr<void> flush_output_buffer(NonnullRefPtr<Connection> connection);

    ErrorOr<void> send_handshake(HandshakeMessage handshake, NonnullRefPtr<Connection> connection);
    void close_connection_internal(NonnullRefPtr<Connection> connection, DeprecatedString error_message, bool invoke_callback = true);

    ErrorOr<void> on_ready_to_accept();
};

}
