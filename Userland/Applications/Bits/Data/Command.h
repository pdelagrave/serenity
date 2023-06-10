/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../Torrent.h"
#include "PeerContext.h"
#include "TorrentContext.h"
#include <LibCore/Event.h>
namespace Bits::Data {

class Command : public Core::CustomEvent {
public:
    enum class Type {
        AddPeers,
        ActivateTorrent,
        PieceDownloaded
    };
    virtual ~Command() = default;

protected:
    explicit Command(Type type)
        : Core::CustomEvent(to_underlying(type))
    {
    }
};

struct AddPeersCommand : public Command {
    AddPeersCommand(ReadonlyBytes info_hash, Vector<Core::SocketAddress> peers)
        : Command(Command::Type::AddPeers)
        , info_hash(move(info_hash))
        , peers(move(peers))
    {
    }
    ReadonlyBytes info_hash;
    Vector<Core::SocketAddress> peers;
};

struct ActivateTorrentCommand : public Command {
    ActivateTorrentCommand(NonnullRefPtr<TorrentContext> tcontext)
        : Command(Command::Type::ActivateTorrent)
        , torrent_context(move(tcontext))
    {
    }
    NonnullRefPtr<TorrentContext> torrent_context;
};

class PieceDownloadedCommand : public Command {
public:
    explicit PieceDownloadedCommand(u64 index, ReadonlyBytes data, NonnullRefPtr<PeerContext> pcontext)
        : Command(Command::Type::PieceDownloaded)
        , m_index(index)
        , m_data(ByteBuffer::copy(data).release_value())
        , m_peer_context(move(pcontext))

    {
    }

    u64 index() const { return m_index; }
    ReadonlyBytes data() const { return m_data.bytes(); }
    NonnullRefPtr<PeerContext> const& peer_context() const { return m_peer_context; }

private:
    u64 m_index;
    ByteBuffer m_data;
    NonnullRefPtr<PeerContext> m_peer_context;
};

}
