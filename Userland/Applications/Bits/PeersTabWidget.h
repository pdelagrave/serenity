/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "BitsUiEvents.h"
#include <LibGUI/TableView.h>
#include <LibGUI/Widget.h>

namespace Bits {
struct TorrentContext;
}

class PeersTabWidget : public GUI::Widget {
    C_OBJECT(PeersTabWidget)
public:
    PeersTabWidget(Function<Optional<NonnullRefPtr<Bits::TorrentContext>>()> get_current_torrent);
    void refresh();
protected:
    void custom_event(Core::CustomEvent& event) override;
private:
    void set_torrent(Optional<NonnullRefPtr<Bits::TorrentContext>> torrent);
    Function<Optional<NonnullRefPtr<Bits::TorrentContext>>()> m_get_current_torrent;
    RefPtr<GUI::TableView> m_peers_view;
};
