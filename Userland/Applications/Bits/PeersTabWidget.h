/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "LibBits/TorrentView.h"
#include <LibGUI/TableView.h>
#include <LibGUI/Widget.h>

class PeersTabWidget : public GUI::Widget {
    C_OBJECT(PeersTabWidget)
public:
    PeersTabWidget();
    void update(Vector<Bits::PeerView>);
private:
    RefPtr<GUI::TableView> m_peers_table_view;
};
