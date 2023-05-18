/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "Command.h"
#include "Torrent.h"
#include <LibCore/Event.h>
#include <LibCore/Object.h>
#include <LibProtocol/RequestClient.h>
namespace Bits {

class Engine : public Core::Object {
    C_OBJECT(Engine);

public:
    enum class EngineEventType {
        Command
    };
    static ErrorOr<NonnullRefPtr<Engine>> try_create();

    Vector<NonnullRefPtr<Torrent>> torrents() { return m_torrents; }
    void post(NonnullOwnPtr<Command>);

private:
    Engine(NonnullRefPtr<Protocol::RequestClient>);
    static ErrorOr<String> url_encode_bytes(u8 const* bytes, size_t length);
    static ErrorOr<String> hexdump(ReadonlyBytes);

    NonnullRefPtr<Protocol::RequestClient> m_protocol_client;
    HashTable<NonnullRefPtr<Protocol::Request>> m_active_requests;

    Vector<NonnullRefPtr<Torrent>> m_torrents;
    ErrorOr<void> announce(Torrent&);

protected:
    void custom_event(Core::CustomEvent& event) override;
};

class CommandEvent : public Core::CustomEvent {
public:
    CommandEvent(NonnullOwnPtr<Command> command)
        : Core::CustomEvent((int)Engine::EngineEventType::Command)
        , m_command(move(command))
    {
    }

    NonnullOwnPtr<Command> command() { return move(m_command); }

private:
    NonnullOwnPtr<Command> m_command;
};

}