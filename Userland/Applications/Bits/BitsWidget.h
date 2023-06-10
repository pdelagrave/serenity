/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "Engine.h"
#include "PeersTabWidget.h"
#include "Torrent.h"
#include "TorrentModel.h"
#include <LibGUI/TableView.h>
#include <LibGUI/Widget.h>


namespace Bits {

class BitsWidget final : public GUI::Widget {
    C_OBJECT(BitsWidget)
public:
    static ErrorOr<NonnullRefPtr<BitsWidget>> create(NonnullRefPtr<Engine> engine);
    virtual ~BitsWidget() override = default;
    ErrorOr<void> open_file(String const& filename, NonnullOwnPtr<Core::File>, bool start);

private:
    BitsWidget(NonnullRefPtr<Engine>);
    RefPtr<GUI::Menu> m_torrent_context_menu;

    RefPtr<GUI::TableView> m_torrents_table_view;
    RefPtr<TorrentModel> m_torrent_model;
    RefPtr<Core::Timer> m_update_timer;

    RefPtr<GUI::TabWidget> m_bottom_tab_widget;
    RefPtr<PeersTabWidget> m_peer_list_widget;

    NonnullRefPtr<Engine> m_engine;
};
}