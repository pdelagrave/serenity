/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "AK/NonnullOwnPtr.h"
#include "MetaInfo.h"
namespace Bits {

enum class TorrentState {
    STOPPED,
    STARTED
};
ErrorOr<String> state_to_string(TorrentState state);

class Torrent : public RefCounted<Torrent> {
public:
    Torrent(NonnullOwnPtr<MetaInfo>, String const&);
    MetaInfo& meta_info() { return *m_meta_info; }
    TorrentState state() { return m_state; }
    void set_state(TorrentState state) { m_state = state; }
    String data_path() { return m_data_path; }

private:
    NonnullOwnPtr<MetaInfo> m_meta_info;
    String m_data_path;
    TorrentState m_state;
};

}
