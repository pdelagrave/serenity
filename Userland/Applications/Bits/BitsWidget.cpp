/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "BitsWidget.h"
#include "MetaInfo.h"
#include "Torrent.h"
#include <Applications/Bits/BitsWindowGML.h>
#include <LibFileSystemAccessClient/Client.h>
#include <LibGUI/Action.h>
#include <LibGUI/SortingProxyModel.h>
#include <LibGUI/BoxLayout.h>
#include <LibGUI/Menu.h>

namespace Bits {

ErrorOr<void> BitsWidget::open_file(String const& filename, NonnullOwnPtr<Core::File> file)
{
    dbgln("Opening file {}", filename);
    auto meta_info = TRY(adopt_nonnull_own_or_enomem(TRY(MetaInfo::create(*file))));
    file->close();
    m_torrents.append(TRY(adopt_nonnull_ref_or_enomem(new (nothrow) Torrent(move(meta_info), TRY("/home/anon/Downloads"_string)))));

    return {};
}

void BitsWidget::initialize_menubar(GUI::Window& window)
{
    auto& file_menu = window.add_menu("&File");
    file_menu.add_action(*m_open_action);
}

BitsWidget::BitsWidget()
{
    load_from_gml(get_bits_window_gml).release_value_but_fixme_should_propagate_errors();
    set_layout<GUI::VerticalBoxLayout>();

    m_open_action = GUI::CommonActions::make_open_action([this](auto&) {
        auto response = FileSystemAccessClient::Client::the().open_file(window(), {}, Core::StandardPaths::home_directory(), Core::File::OpenMode::Read);
        if (response.is_error())
            return;

        auto err = open_file(response.value().filename(), response.value().release_stream());
        if (err.is_error())
            err.release_error();
    });

    m_torrent_model = TorrentModel::create([this] { return get_torrents(); });
    m_torrents_table_view = *find_descendant_of_type_named<GUI::TableView>("torrent_table");
    m_torrents_table_view->set_model(m_torrent_model);

    m_update_timer = add<Core::Timer>(
        500, [this] {
            this->m_torrent_model->update();
        });
    m_update_timer->start();
}

Vector<NonnullRefPtr<Torrent>> BitsWidget::get_torrents()
{
    return m_torrents;
}

}