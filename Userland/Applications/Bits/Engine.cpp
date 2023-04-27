/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Engine.h"
#include "LibCore/EventLoop.h"

namespace Bits {

int Engine::start()
{
    m_event_loop = make<Core::EventLoop>();
    return m_event_loop->exec();
}
void Engine::custom_event(Core::CustomEvent& event)
{
    if (is<CommandEvent>(event)) {
        auto command = reinterpret_cast<CommandEvent&>(event).command();
        if (is<AddTorrent>(*command)) {
            auto& add_torrent = reinterpret_cast<AddTorrent&>(*command);
            m_torrents.append(adopt_nonnull_ref_or_enomem(new (nothrow) Torrent(add_torrent.meta_info(), add_torrent.data_path())).release_value());
        } else if (is<StartTorrent>(*command)) {
            auto& start_torrent = reinterpret_cast<StartTorrent&>(*command);
            m_torrents.at(start_torrent.torrent_id())->set_state(TorrentState::STARTED);
        } else if (is<StopTorrent>(*command)) {
            auto& stop_torrent = reinterpret_cast<StopTorrent&>(*command);
            m_torrents.at(stop_torrent.torrent_id())->set_state(TorrentState::STOPPED);
        }
    }
}
void Engine::post(NonnullOwnPtr<Command> command)
{
    m_event_loop->post_event(*this, make<CommandEvent>(move(command)), Core::EventLoop::ShouldWake::Yes);
}

}