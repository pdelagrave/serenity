/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "BitsWidget.h"
#include "Engine.h"
#include "MetaInfo.h"
#include "Torrent.h"
#include <Applications/Bits/BitsWindowGML.h>
#include <LibFileSystemAccessClient/Client.h>
#include <LibGUI/Action.h>
#include <LibGUI/BoxLayout.h>
#include <LibGUI/Menu.h>
#include <LibGUI/MessageBox.h>
#include <LibGUI/SortingProxyModel.h>

namespace Bits {

ErrorOr<void> BitsWidget::open_file(String const& filename, NonnullOwnPtr<Core::File> file)
{
    dbgln("Opening file {}", filename);
    auto meta_info = TRY(MetaInfo::create(*file));
    file->close();
    m_engine->add_torrent(move(meta_info), Core::StandardPaths::downloads_directory());

    return {};
}

void BitsWidget::initialize_menubar(GUI::Window& window)
{
    auto& file_menu = window.add_menu("&File"_string.release_value());
    file_menu.add_action(*m_open_action);
}

BitsWidget::BitsWidget(NonnullRefPtr<Engine> engine)
    : m_engine(move(engine))
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

    auto start_torrent_action = GUI::Action::create("Start",
        [&](GUI::Action&) {
            m_torrents_table_view->selection().for_each_index([&](GUI::ModelIndex const& index) {
                m_engine->start_torrent(index.row());
            });
        });

    auto stop_torrent_action = GUI::Action::create("Stop",
        [&](GUI::Action&) {
            m_torrents_table_view->selection().for_each_index([&](GUI::ModelIndex const& index) {
                m_engine->stop_torrent(index.row());
            });
        });

    m_torrent_model = TorrentModel::create([this] { return m_engine->torrents(); });
    m_torrents_table_view = *find_descendant_of_type_named<GUI::TableView>("torrent_table");
    m_torrents_table_view->set_model(m_torrent_model);
    m_torrents_table_view->set_selection_mode(GUI::AbstractView::SelectionMode::MultiSelection);
    m_torrents_table_view->on_context_menu_request = [this, start_torrent_action, stop_torrent_action](const GUI::ModelIndex& model_index, const GUI::ContextMenuEvent& event) {
        if (model_index.is_valid()) {
            m_torrent_context_menu = GUI::Menu::construct();
            if (m_engine->torrents().at(model_index.row())->state() == TorrentState::STOPPED)
                m_torrent_context_menu->add_action(start_torrent_action);
            else
                m_torrent_context_menu->add_action(stop_torrent_action);

            m_torrent_context_menu->popup(event.screen_position());
        }
    };

    m_update_timer = add<Core::Timer>(
        500, [this] {
            this->m_torrent_model->update();
        });
    m_update_timer->start();
}

}