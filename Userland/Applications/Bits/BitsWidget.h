/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "GeneralTorrentInfoWidget.h"
#include "PeersTabWidget.h"
#include <LibBitTorrent/Engine.h>
#include <LibGUI/TableView.h>
#include <LibGUI/Widget.h>

class TorrentModel final : public GUI::Model {
public:
    virtual int column_count(GUI::ModelIndex const& index = GUI::ModelIndex()) const override;
    virtual ErrorOr<String> column_name(int i) const override;
    virtual GUI::Variant data(GUI::ModelIndex const& index, GUI::ModelRole role) const override;
    virtual int row_count(GUI::ModelIndex const& index = GUI::ModelIndex()) const override;

    Bits::TorrentView torrent_at(int index) const;
    void update(NonnullOwnPtr<HashMap<Bits::InfoHash, Bits::TorrentView>>);

private:
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

    NonnullOwnPtr<HashMap<Bits::InfoHash, Bits::TorrentView>> m_torrents { make<HashMap<Bits::InfoHash, Bits::TorrentView>>() };
    Vector<Bits::InfoHash> m_hashes;
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
    RefPtr<GeneralTorrentInfoWidget> m_general_widget;
    RefPtr<PeersTabWidget> m_peer_list_widget;

    NonnullRefPtr<Bits::Engine> m_engine;
};