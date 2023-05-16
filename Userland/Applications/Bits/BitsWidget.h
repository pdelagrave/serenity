/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "Engine.h"
#include "Torrent.h"
#include "TorrentModel.h"
#include <LibGUI/TableView.h>
#include <LibGUI/Widget.h>

namespace Bits {

class BitsWidget final : public GUI::Widget {
    C_OBJECT(BitsWidget)
public:
    virtual ~BitsWidget() override = default;
    ErrorOr<void> open_file(String const& filename, NonnullOwnPtr<Core::File>);
    void initialize_menubar(GUI::Window&);

private:
    BitsWidget(NonnullRefPtr<Engine>);
    RefPtr<GUI::Action> m_open_action;
    RefPtr<GUI::Menu> m_torrent_context_menu;

    RefPtr<GUI::TableView> m_torrents_table_view;
    RefPtr<TorrentModel> m_torrent_model;
    RefPtr<Core::Timer> m_update_timer;

    NonnullRefPtr<Engine> m_engine;
};
}