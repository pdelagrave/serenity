/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "BitsUiEvents.h"
#include "Torrent.h"
#include <LibGUI/Widget.h>
#include <LibGUI/TableView.h>

namespace Bits {

class PeersTabWidget : public GUI::Widget {
    C_OBJECT(PeersTabWidget)
public:
    PeersTabWidget(Function<Optional<NonnullRefPtr<Torrent>>()> get_current_torrent);
    void refresh();
protected:
    void custom_event(Core::CustomEvent& event) override;
private:
    void set_torrent(Optional<NonnullRefPtr<Torrent>> torrent);
    Function<Optional<NonnullRefPtr<Torrent>>()> m_get_current_torrent;
    RefPtr<GUI::TableView> m_peers_view;
};

}
