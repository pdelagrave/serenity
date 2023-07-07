/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Comm.h"
#include "HandshakeMessage.h"
// FIXME: Ideally Comm would only know about the abstract Message class and shouldn't be exposed to any specific message types. Handshake and Keepalives are the only special message types and handled by Comm.
#include "../Message.h"
#include <LibCore/System.h>

namespace Bits {

Comm::Comm(u16 const listen_port)
    : m_server(Core::TCPServer::try_create(this).release_value())
{
    m_thread = Threading::Thread::construct([this, listen_port]() -> intptr_t {
        m_event_loop = make<Core::EventLoop>();

        auto err = m_server->set_blocking(false);
        if (err.is_error()) {
            dbgln("Failed to set server to blocking mode: {}", err.error());
            return 1;
        }

        m_server->on_ready_to_accept = [&] {
            auto err = on_ready_to_accept();
            if (err.is_error())
                dbgln("Failed to accept connection: {}", err.error());
        };
        err = m_server->listen(IPv4Address::from_string("0.0.0.0"sv).release_value(), listen_port);
        if (err.is_error()) {
            dbgln("Failed to listen: {}", err.error());
            return 1;
        }

        gettimeofday(&m_last_speed_measurement, nullptr);
        start_timer(1000);
        return m_event_loop->exec();
    },
        "Comm"sv);

    m_thread->start();
}

void Comm::close_connection(Bits::ConnectionId connection_id, DeprecatedString reason)
{
    m_event_loop->deferred_invoke([&, connection_id, reason] {
        auto connection = m_connections.get(connection_id);
        if (connection.has_value())
            close_connection_internal(*connection.release_value(), reason);
        // else connection not found here because it was already closed by the remote host. The engine event loop didn't invoke the related callback yet and called close_connection thinking it was still open.
    });
}

void Comm::timer_event(Core::TimerEvent&)
{
    // TODO clean this up, put each in their own method, also make it so that we can have different intervals

    // Transfer speed measurement
    timeval current_time;
    timeval time_diff;
    gettimeofday(&current_time, nullptr);
    timersub(&current_time, &m_last_speed_measurement, &time_diff);
    auto time_diff_ms = time_diff.tv_sec * 1000 + time_diff.tv_usec / 1000;
    for (auto const& [cid, c] : m_connections) {
        auto& stats = m_connection_stats.get(cid).release_value();

        c->download_speed = (c->bytes_downloaded_since_last_speed_measurement / time_diff_ms) * 1000;
        stats.download_speed = c->download_speed;
        stats.bytes_downloaded += c->bytes_downloaded_since_last_speed_measurement;
        c->bytes_downloaded_since_last_speed_measurement = 0;

        c->upload_speed = (c->bytes_uploaded_since_last_speed_measurement / time_diff_ms) * 1000;
        stats.upload_speed = c->upload_speed;
        stats.bytes_uploaded += c->bytes_uploaded_since_last_speed_measurement;
        c->bytes_uploaded_since_last_speed_measurement = 0;
    }
    m_last_speed_measurement = current_time;
    on_connection_stats_update(make<HashMap<ConnectionId, ConnectionStats>>(m_connection_stats));

    // Peers keepalive
    auto keepalive_timeout = Duration::from_seconds(120);
    auto now = Core::DateTime::now();
    for (auto const& [cid, c] : m_connections) {
        if (now.timestamp() - c->last_message_received_at.timestamp() > keepalive_timeout.to_milliseconds() + 10000) {
            close_connection_internal(c, "Peer timed out");
            continue;
        }

        if (now.timestamp() - c->last_message_sent_at.timestamp() > keepalive_timeout.to_milliseconds() - 10000) {
            dbgln("Sending keepalive");
            send_message(cid, make<KeepAliveMessage>());
        }
    }

    // TODO add connecting time outs
    // TODO add handshake callbacks time outs
}

ErrorOr<void> Comm::read_from_socket(NonnullRefPtr<Connection> connection)
{
    if (connection->handshake_received && !connection->session_established) {
        return {}; // Still waiting for the Engine to call us back for the decision about this connection.
    }

    for (;;) {
        auto nread_or_error = connection->input_message_buffer.fill_from_stream(*connection->socket);
        if (connection->socket->is_eof()) {
            close_connection_internal(connection, "Remote host closed the connection");
            return {};
        }
        if (nread_or_error.is_error()) {
            auto code = nread_or_error.error().code();
            if (code == EINTR) {
                continue;
            } else if (code == EAGAIN) {
                break;
            } else {
                close_connection_internal(connection, DeprecatedString::formatted("Error reading from socket: err: {}  code: {}  codestr: {}", nread_or_error.error(), code, strerror(code)));
                return {};
            }
        }
        connection->bytes_downloaded_since_last_speed_measurement += nread_or_error.value();
    }

    while (connection->input_message_buffer.used_space() >= connection->incoming_message_length) {
        if (connection->incoming_message_length > 0) {
            auto buffer = TRY(ByteBuffer::create_uninitialized(connection->incoming_message_length));
            VERIFY(connection->input_message_buffer.read(buffer.bytes()).size() == connection->incoming_message_length);

            connection->incoming_message_length = 0;
            connection->last_message_received_at = Core::DateTime::now();

            if (!connection->handshake_received) {
                auto handshake = HandshakeMessage(buffer.bytes());
                dbgln("Received handshake: {}", handshake.to_string());
                connection->handshake_received = true;
                connection->socket->set_notifications_enabled(false); // Because we don't want to read and parse messages from that peer until we have accepted the handshake.

                if (connection->handshake_sent) {
                    on_handshake_from_outgoing_connection(connection->id, handshake, [&, connection] (bool accepted) {
                        m_event_loop->deferred_invoke([&, connection, accepted] {
                            if (accepted) {
                                connection->session_established = true;
                                on_connection_established(connection->id);
                                connection->socket->on_ready_to_read();
                                connection->socket->set_notifications_enabled(true);
                            } else {
                                close_connection_internal(connection, "Disconnecting based on received handshake");
                            }
                        });
                    });
                } else {
                    on_handshake_from_incoming_connection(connection->id, handshake, connection->socket->address(), [&, connection] (Optional<HandshakeMessage> maybe_handshake_to_send) {
                        m_event_loop->deferred_invoke([&, connection, maybe_handshake_to_send] {
                            if (maybe_handshake_to_send.has_value()) {
                                auto err = send_handshake(maybe_handshake_to_send.value(), connection);
                                if (err.is_error()) {
                                    close_connection_internal(connection, "Error sending handshake");
                                    return;
                                }
                                connection->session_established = true;
                                on_connection_established(connection->id);
                                connection->socket->on_ready_to_read();
                                connection->socket->set_notifications_enabled(true);
                            } else {
                                close_connection_internal(connection, "Connection request rejected based on received handshake");
                            }
                        });
                    });
                }
                return {}; // Also because we don't want to read and parse more messages from that peer until we have accepted the handshake.
            } else {
                on_message_receive(connection->id, buffer.bytes());
            }
        } else if (connection->input_message_buffer.used_space() >= sizeof(connection->incoming_message_length)) {
            connection->input_message_buffer.read({ &connection->incoming_message_length, sizeof(connection->incoming_message_length) });
            if (connection->incoming_message_length == 0) {
                dbgln("Received keep-alive");
                connection->last_message_received_at = Core::DateTime::now();
            }
        } else {
            // Not enough bytes to read the length of the next message
            return {};
        }
    }
    return {};
}

ErrorOr<void> Comm::flush_output_buffer(NonnullRefPtr<Connection> connection)
{
    // VERIFY(peer->output_message_buffer.used_space() > 0);
    if (connection->output_message_buffer.used_space() == 0) {
        dbgln("Nothing to flush!");
    }

    for (;;) {
        auto err = connection->output_message_buffer.flush_to_stream(*connection->socket);
        if (err.is_error()) {
            auto code = err.error().code();
            if (code == EINTR) {
                continue;
            } else if (code == EAGAIN) {
                dbgln("Socket is not ready to write, enabling read to write notifier");
                connection->write_notifier->set_enabled(true);
            } else {
                dbgln("Error writing to socket: err: {}  code: {}  codestr: {}", err.error(), code, strerror(code));
                return Error::from_errno(code);
            }
            return {};
        }
        connection->bytes_uploaded_since_last_speed_measurement += err.release_value();

        if (connection->output_message_buffer.used_space() == 0) {
            //            dbglnc(parent_context, context, "Output message buffer is empty, we sent everything, disabling ready to write notifier");
            connection->write_notifier->set_enabled(false);
            return {};
        }
    }
}

ErrorOr<ConnectionId> Comm::connect(Core::SocketAddress address, HandshakeMessage handshake)
{
    auto socket_fd = TRY(Core::System::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0));

    auto sockaddr = address.to_sockaddr_in();
    auto connect_err = Core::System::connect(socket_fd, bit_cast<struct sockaddr*>(&sockaddr), sizeof(sockaddr));
    if (connect_err.is_error() && connect_err.error().code() != EINPROGRESS)
        return connect_err.release_error();

    auto connection = TRY(create_connection(TRY(Core::TCPSocket::adopt_fd(socket_fd))));

    connection->write_notifier->on_activation = [&, socket_fd, connection, handshake] {
        // We were already connected and we can write again:
        if (connection->handshake_sent) {
            auto err = flush_output_buffer(connection);
            if (err.is_error()) {
                close_connection_internal(connection, DeprecatedString::formatted("Error flushing output buffer: {}", err.release_error()));
            }
            return;
        }

        // We were trying to connect, we can now figure out if it succeeded or not:
        int so_error;
        socklen_t len = sizeof(so_error);
        auto ret = getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &so_error, &len);
        if (ret == -1) {
            auto errn = errno;
            close_connection_internal(connection, DeprecatedString::formatted("Error calling getsockopt when verifying if the connect() succeeded: errno: {} {}", errn, strerror(errn)));
            return;
        }

        if (so_error == ESUCCESS) {
            connection->write_notifier->set_enabled(false);
            connection->socket->on_ready_to_read = [&, connection] {
                auto err = read_from_socket(connection);
                if (err.is_error()) {
                    close_connection_internal(connection, DeprecatedString::formatted("Error reading from socket: {}", err.release_error()));
                }
            };
            auto err = send_handshake(handshake, connection);
            if (err.is_error()) {
                close_connection_internal(connection, DeprecatedString::formatted("Error sending handshake for outgoing connection: {}", err.release_error()));
            }
        } else {
            // Would be nice to have GNU extension strerrorname_np() so we can print ECONNREFUSED,... too.
            close_connection_internal(connection, DeprecatedString::formatted("Error connecting: so_error: {}", strerror(so_error)));
        }
    };

    // FIXME: Hack to make the notifier enabled on the Comm thread/eventloop. Simpler for now to have the Comm::connect() method to be synchronous because of the Connection() constructor and connection id generation.
    m_event_loop->deferred_invoke([&, write_notifier = connection->write_notifier] {
        write_notifier->set_enabled(true);
    });

    return connection->id;
}

void Comm::send_message(ConnectionId connection_id, NonnullOwnPtr<Message> message)
{
    auto connection = m_connections.get(connection_id).value();
    auto size = BigEndian<u32>(message->size());
    dbgln("Sending message [{}b] {}", size, *message);
    size_t total_size = message->size() + sizeof(u32); // message size + message payload
    if (connection->output_message_buffer.empty_space() < total_size) {
        // TODO: keep a non-serialized message queue?
        // FIXME: Choke peer?
        dbgln("Outgoing message buffer is full, dropping message");
        return;
    }

    connection->output_message_buffer.write({ &size, sizeof(u32) });
    connection->output_message_buffer.write(message->serialized);

    auto err = flush_output_buffer(*connection);
    if (err.is_error()) {
        close_connection_internal(*connection, DeprecatedString::formatted("Error flushing output buffer when sending message: {}", err.release_error()));
        return;
    }

    connection->last_message_sent_at = Core::DateTime::now();
}

ErrorOr<void> Comm::send_handshake(HandshakeMessage handshake, NonnullRefPtr<Connection> connection)
{
    dbgln("Sending handshake: {}", handshake.to_string());
    connection->output_message_buffer.write({ &handshake, sizeof(handshake) });
    TRY(flush_output_buffer(connection));
    connection->handshake_sent = true;
    return {};
}

void Comm::close_connection_internal(NonnullRefPtr<Connection> connection, DeprecatedString error_message)
{
    connection->write_notifier->close();
    connection->socket->close();
    connection->socket->on_ready_to_read = nullptr;

    m_connection_stats.remove(connection->id);
    m_connections.remove(connection->id);

    // Assuming that in all cases, if we sent the handshake, the engine is aware of the connection, and it's safe to invoke the callback.
    if (connection->handshake_sent)
        on_peer_disconnect(connection->id, error_message);
}

ErrorOr<void> Comm::on_ready_to_accept()
{
    auto accepted_socket = TRY(m_server->accept());
    TRY(accepted_socket->set_blocking(false));

    auto connection = TRY(create_connection(move(accepted_socket)));

    connection->write_notifier->on_activation = [&, connection] {
        auto err = flush_output_buffer(connection);
        if (err.is_error()) {
            close_connection_internal(connection, DeprecatedString::formatted("Error flushing output buffer for accepted connection: {}", err.release_error()));
        }
    };

    connection->socket->on_ready_to_read = [&, connection] {
        auto err = read_from_socket(connection);
        if (err.is_error()) {
            close_connection_internal(connection, DeprecatedString::formatted("Error reading from (accepted) socket: {}", err.release_error()));
        }
    };
    return {};
}

ErrorOr<NonnullRefPtr<Connection>> Comm::create_connection(NonnullOwnPtr<Core::TCPSocket> socket)
{
    NonnullRefPtr<Core::Notifier> write_notifier = Core::Notifier::construct(socket->fd(), Core::Notifier::Type::Write);
    // Initial state should be enabled when creating an outbound connection, disabled when accepting an inbound connection.
    write_notifier->set_enabled(false);

    auto connection = TRY(Connection::try_create(socket, write_notifier, 1 * MiB, 1 * MiB));
    m_connections.set(connection->id, connection);
    m_connection_stats.set(connection->id, ConnectionStats {});

    return connection;
}

}
