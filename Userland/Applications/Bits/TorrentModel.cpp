/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "TorrentModel.h"
#include <AK/NumberFormat.h>

namespace Bits {
int TorrentModel::row_count(GUI::ModelIndex const&) const
{
    return m_torrents.size();
}
int TorrentModel::column_count(GUI::ModelIndex const&) const
{
    return m_columns.size();
}
GUI::Variant TorrentModel::data(GUI::ModelIndex const& index, GUI::ModelRole role) const
{
    if (role != GUI::ModelRole::Display)
        return {};
    if (!is_within_range(index))
        return {};

    // TODO: progress in the taskbar
    // warn("\033]9;{};{};\033\\", downloaded_size, maybe_total_size.value());
    auto torrent = m_torrents.at(index.row());
    MetaInfo& meta_info = torrent->meta_info();
    if (index.column() == 0)
        return torrent->display_name();
    else if (index.column() == 1)
        return AK::human_readable_quantity(meta_info.total_length());
    else if (index.column() == 2)
        return state_to_string(torrent->state()).release_value_but_fixme_should_propagate_errors();
    else if (index.column() == 3)
        return DeprecatedString::formatted("{}%", torrent->progress());
    else if (index.column() == 4)
        return "torrent->data_path()";
    else
        return "??";
}

void TorrentModel::update()
{
    m_torrents = m_get_updated_torrent_list();
    did_update(UpdateFlag::DontInvalidateIndices);
}
String TorrentModel::column_name(int i) const
{
    return m_columns.at(i);
}

}