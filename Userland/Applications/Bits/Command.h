/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "AK/NonnullOwnPtr.h"
#include "MetaInfo.h"
namespace Bits {

class Command {
public:
    virtual ~Command() = default;
protected:
    Command() {};
};

class AddTorrent : public Command {
public:
    AddTorrent(NonnullOwnPtr<MetaInfo> meta_info, String data_path)
        : m_meta_info(move(meta_info))
        , m_data_path(data_path)
    {}

    NonnullOwnPtr<MetaInfo> meta_info() { return move(m_meta_info); }
    String data_path() { return m_data_path; }

private:
    NonnullOwnPtr<MetaInfo> m_meta_info;
    String m_data_path;
};

class StartTorrent : public Command {
public:
    StartTorrent(int torrent_id) : m_torrent_id(torrent_id) {}
    int torrent_id() { return m_torrent_id; }
private:
    int m_torrent_id;
};

class StopTorrent : public Command {
public:
    StopTorrent(int torrent_id) : m_torrent_id(torrent_id) {}
    int torrent_id() { return m_torrent_id; }
private:
    int m_torrent_id;
};

}
