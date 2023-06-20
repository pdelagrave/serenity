/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "TorrentModel.h"
#include "Userland/Applications/Bits/LibBits/Engine.h"
#include "Userland/Applications/Bits/LibBits/Net/PeerContext.h"
#include "Userland/Applications/Bits/LibBits/Net/PieceHeap.h"
#include "Userland/Applications/Bits/LibBits/Net/TorrentContext.h"
#include "Userland/Applications/Bits/LibBits/Torrent.h"
#include <AK/NumberFormat.h>

namespace Bits {

int TorrentModel::row_count(GUI::ModelIndex const&) const
{
    return m_torrents.size();
}
int TorrentModel::column_count(GUI::ModelIndex const&) const
{
    return Column::__Count;
}
GUI::Variant TorrentModel::data(GUI::ModelIndex const& index, GUI::ModelRole role) const
{
    if (role == GUI::ModelRole::TextAlignment)
        return Gfx::TextAlignment::CenterLeft;
    if (role == GUI::ModelRole::Display) {
        auto torrent = Engine::s_engine->torrents().at(index.row());
        MetaInfo& meta_info = torrent->meta_info();
        auto tcontext = m_torrents.at(index.row());

        switch (index.column()) {
        case Column::Name:
            return torrent->display_name();
        case Column::Size:
            return AK::human_readable_quantity(meta_info.total_length());
        case Column::State:
            return state_to_string(torrent->state()).release_value_but_fixme_should_propagate_errors();
        case Column::Progress:
            return DeprecatedString::formatted("{:.1}%", torrent->state() == TorrentState::CHECKING ? torrent->check_progress() : tcontext->local_bitfield.progress());
        case Column::DownloadSpeed:
            return DeprecatedString::formatted("{}/s", human_readable_size(tcontext->download_speed));
        case Column::UploadSpeed:
            return DeprecatedString::formatted("{}/s", human_readable_size(tcontext->upload_speed));
        case Column::Path:
            return torrent->data_path();
        default:
            VERIFY_NOT_REACHED();
        }
    }
    return {};
}

void TorrentModel::update()
{
    m_torrents = m_get_updated_torrent_list();
    did_update(UpdateFlag::DontInvalidateIndices);
}
String TorrentModel::column_name(int column) const
{
    switch (column) {
    case Column::Name:
        return "Name"_short_string;
    case Column::Size:
        return "Size"_short_string;
    case Column::State:
        return "State"_string.release_value();
    case Column::Progress:
        return "Progress"_string.release_value();
    case Column::DownloadSpeed:
        return "Download Speed"_string.release_value();
    case Column::UploadSpeed:
        return "Upload Speed"_string.release_value();
    case Column::Path:
        return "Path"_string.release_value();
    default:
        VERIFY_NOT_REACHED();
    }
}

}