/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "Command.h"
#include "LibCore/Object.h"
#include "Torrent.h"
#include <LibCore/Event.h>
namespace Bits {

class Engine : public Core::Object {
    C_OBJECT(Engine);

public:
    enum class EngineEventType {
        Command
    };

    int start();
    Vector<NonnullRefPtr<Torrent>> torrents() { return m_torrents; }
    void post(NonnullOwnPtr<Command>);

private:
    Vector<NonnullRefPtr<Torrent>> m_torrents;
    OwnPtr<Core::EventLoop> m_event_loop;

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