/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../Torrent.h"
#include "PeerContext.h"
#include <LibCore/Event.h>
namespace Bits::Data {

class Command : public Core::Event {
public:
    enum class Type {
        // GUI to engine commands.
        PieceDownloaded = 78000,
    };
    virtual ~Command() = default;

protected:
    explicit Command(Type type)
        : Core::Event(to_underlying(type))
    {
    }
};

class PieceDownloadedCommand : public Command {
public:
    explicit PieceDownloadedCommand(u64 index, ReadonlyBytes data, NonnullRefPtr<PeerContext> context)
        : Command(Command::Type::PieceDownloaded)
        , m_index(index)
        , m_data(ByteBuffer::copy(data).release_value())
        , m_context(move(context))

    {
    }

    u64 index() const { return m_index; }
    ReadonlyBytes data() const { return m_data.bytes(); }
    NonnullRefPtr<PeerContext> const& context() const { return m_context; }

private:
    u64 m_index;
    ByteBuffer m_data;
    NonnullRefPtr<PeerContext> m_context;
};

}
