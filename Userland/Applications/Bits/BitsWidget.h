/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "PeersTabWidget.h"
#include "Userland/Applications/Bits/LibBits/Engine.h"
#include "Userland/Applications/Bits/LibBits/Torrent.h"
#include <LibGUI/TableView.h>
#include <LibGUI/Widget.h>

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

class BitsWidget final : public GUI::Widget {
    C_OBJECT(BitsWidget)
public:
    static ErrorOr<NonnullRefPtr<BitsWidget>> create(NonnullRefPtr<Bits::Engine> engine, GUI::Window* window);
    virtual ~BitsWidget() override = default;
    void open_file(String const& filename, NonnullOwnPtr<Core::File>, bool start);

private:
    BitsWidget(NonnullRefPtr<Bits::Engine>);
    RefPtr<GUI::Menu> m_torrent_context_menu;

    RefPtr<GUI::TableView> m_torrents_table_view;
    RefPtr<TorrentModel> m_torrent_model;
    RefPtr<Core::Timer> m_update_timer;

    RefPtr<GUI::TabWidget> m_bottom_tab_widget;
    RefPtr<PeersTabWidget> m_peer_list_widget;

    NonnullRefPtr<Bits::Engine> m_engine;
};