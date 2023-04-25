/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "LibGUI/Widget.h"
#include "Torrent.h"
#include "TorrentModel.h"
#include <LibGUI/TableView.h>

namespace Bits {

class BitsWidget final : public GUI::Widget {
    C_OBJECT(BitsWidget)
public:
    virtual ~BitsWidget() override = default;
    ErrorOr<void> open_file(String const& filename, NonnullOwnPtr<Core::File>);
    void initialize_menubar(GUI::Window&);
    Vector<NonnullRefPtr<Torrent>> get_torrents();

private:
    BitsWidget();
    RefPtr<GUI::Action> m_open_action;

    Vector<NonnullRefPtr<Torrent>> m_torrents;
    RefPtr<GUI::TableView> m_torrents_table_view;
    RefPtr<TorrentModel> m_torrent_model;
    RefPtr<Core::Timer> m_update_timer;
};
}