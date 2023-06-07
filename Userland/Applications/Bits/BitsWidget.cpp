/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "BitsWidget.h"
#include "Engine.h"
#include "MetaInfo.h"
#include "Torrent.h"
#include <LibFileSystemAccessClient/Client.h>
#include <LibGUI/Action.h>
#include <LibGUI/BoxLayout.h>
#include <LibGUI/Menu.h>
#include <LibGUI/SortingProxyModel.h>

namespace Bits {

ErrorOr<void> BitsWidget::open_file(String const& filename, NonnullOwnPtr<Core::File> file)
{
    dbgln("Opening file {}", filename);
    auto meta_info = TRY(MetaInfo::create(*file));
    file->close();
    m_engine->add_torrent(move(meta_info), Core::StandardPaths::downloads_directory());

    m_engine->start_torrent(0);
    return {};
}

BitsWidget::BitsWidget(NonnullRefPtr<Engine> engine)
    : m_engine(move(engine))
{
    set_layout<GUI::VerticalBoxLayout>();

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

    auto cancel_checking_torrent_action = GUI::Action::create("Cancel checking",
        [&](GUI::Action&) {
            m_torrents_table_view->selection().for_each_index([&](GUI::ModelIndex const& index) {
                m_engine->cancel_checking(index.row());
            });
        });

    m_torrent_model = TorrentModel::create([this] { return m_engine->torrents(); });
    m_torrents_table_view = add<GUI::TableView>();
    m_torrents_table_view->set_model(m_torrent_model);
    m_torrents_table_view->set_selection_mode(GUI::AbstractView::SelectionMode::MultiSelection);
    m_torrents_table_view->on_context_menu_request = [this, start_torrent_action, stop_torrent_action, cancel_checking_torrent_action](const GUI::ModelIndex& model_index, const GUI::ContextMenuEvent& event) {
        if (model_index.is_valid()) {
            m_torrent_context_menu = GUI::Menu::construct();
            TorrentState state = m_engine->torrents().at(model_index.row())->state();
            if (state == TorrentState::STOPPED || state == TorrentState::ERROR)
                m_torrent_context_menu->add_action(start_torrent_action);
            else if (state == TorrentState::STARTED)
                m_torrent_context_menu->add_action(stop_torrent_action);
            else if (state == TorrentState::CHECKING)
                m_torrent_context_menu->add_action(cancel_checking_torrent_action);

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