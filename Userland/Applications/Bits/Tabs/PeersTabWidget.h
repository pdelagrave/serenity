/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "Userland/Applications/Bits/LibBits/TorrentView.h"
#include "Userland/Libraries/LibGUI/TableView.h"
#include "Userland/Libraries/LibGUI/Widget.h"

class PeersTabWidget : public GUI::Widget {
    C_OBJECT(PeersTabWidget)
public:
    PeersTabWidget();
    void update(Vector<Bits::PeerView>);
private:
    RefPtr<GUI::TableView> m_peers_table_view;
};
