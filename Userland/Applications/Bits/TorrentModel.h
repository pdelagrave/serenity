/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibGUI/Model.h>

namespace Bits {
struct PeerContext;
struct TorrentContext;
struct PieceStatus;
class PieceHeap;
}

using TorrentListCallback = Function<Vector<NonnullRefPtr<Bits::TorrentContext>>()>;

class TorrentModel final : public GUI::Model {
public:
    int row_count(GUI::ModelIndex const& index) const override;
    int column_count(GUI::ModelIndex const& index) const override;
    GUI::Variant data(GUI::ModelIndex const& index, GUI::ModelRole role) const override;
    String column_name(int i) const override;

    static NonnullRefPtr<TorrentModel> create(TorrentListCallback callback)
    {
        return adopt_ref(*new TorrentModel(move(callback)));
    }
    void update();

private:
    TorrentModel(TorrentListCallback callback)
        : m_get_updated_torrent_list(move(callback)) {};

    enum Column {
        Name,
        Size,
        State,
        Progress,
        DownloadSpeed,
        UploadSpeed,
        Path,
        __Count
    };

    TorrentListCallback m_get_updated_torrent_list;
    Vector<NonnullRefPtr<Bits::TorrentContext>> m_torrents;
};

