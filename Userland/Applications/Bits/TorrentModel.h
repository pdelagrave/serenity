/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "LibGUI/Model.h"
#include "Torrent.h"
namespace Bits {

using TorrentListCallback = Function<Vector<NonnullRefPtr<Torrent>>()>;

class TorrentModel final : public GUI::Model {
public:
    int row_count(GUI::ModelIndex const& index) const override;
    int column_count(GUI::ModelIndex const& index) const override;
    GUI::Variant data(GUI::ModelIndex const& index, GUI::ModelRole role) const override;
    DeprecatedString column_name(int i) const override;

    static NonnullRefPtr<TorrentModel> create(TorrentListCallback callback)
    {
        return adopt_ref(*new TorrentModel(move(callback)));
    }
    void update();

private:
    TorrentModel(TorrentListCallback callback)
        : m_get_updated_torrent_list(move(callback)) {};

    TorrentListCallback m_get_updated_torrent_list;
    Vector<NonnullRefPtr<Torrent>> m_torrents;
    Vector<String> m_columns = { "Name"_string.release_value(), "Size"_string.release_value(), "State"_string.release_value(), "Progress"_string.release_value(), "Local path"_string.release_value() };
};

}
