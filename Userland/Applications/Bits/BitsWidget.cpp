/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "BitsWidget.h"
#include "BitsUiEvents.h"
#include "PeersTabWidget.h"
#include "Userland/Applications/Bits/LibBits/Engine.h"
#include "Userland/Applications/Bits/LibBits/MetaInfo.h"
#include <LibFileSystemAccessClient/Client.h>
#include <LibGUI/Action.h>
#include <LibGUI/BoxLayout.h>
#include <LibGUI/Menu.h>
#include <LibGUI/SortingProxyModel.h>
#include <LibGUI/Splitter.h>

namespace Bits {

ErrorOr<void> BitsWidget::open_file(String const& filename, NonnullOwnPtr<Core::File> file, bool start)
{
    dbgln("Opening file {}", filename);
    auto meta_info = TRY(MetaInfo::create(*file));
    file->close();
    m_engine->add_torrent(move(meta_info), Core::StandardPaths::downloads_directory());

    if (start)
        m_engine->start_torrent(m_engine->torrents().size() - 1);

    if (m_engine->torrents().size() > 0)
        m_torrents_table_view->selection().set(m_torrents_table_view->model()->index(0, 0));

    return {};
}

ErrorOr<NonnullRefPtr<BitsWidget>> BitsWidget::create(NonnullRefPtr<Engine> engine)
{
    auto widget = TRY(adopt_nonnull_ref_or_enomem(new (nothrow) BitsWidget(move(engine))));

    widget->set_layout<GUI::VerticalBoxLayout>();

    auto start_torrent_action = GUI::Action::create("Start",
        [widget](GUI::Action&) {
            widget->m_torrents_table_view->selection().for_each_index([widget](GUI::ModelIndex const& index) {
                widget->m_engine->start_torrent(index.row());
            });
        });

    auto stop_torrent_action = GUI::Action::create("Stop",
        [widget](GUI::Action&) {
            widget->m_torrents_table_view->selection().for_each_index([widget](GUI::ModelIndex const& index) {
                widget->m_engine->stop_torrent(index.row());
            });
        });

    auto cancel_checking_torrent_action = GUI::Action::create("Cancel checking",
        [widget](GUI::Action&) {
            widget->m_torrents_table_view->selection().for_each_index([widget](GUI::ModelIndex const& index) {
                widget->m_engine->cancel_checking(index.row());
            });
        });

    auto& main_splitter = widget->add<GUI::VerticalSplitter>();
    main_splitter.layout()->set_spacing(4);

    widget->m_torrent_model = TorrentModel::create([widget] { return widget->m_engine->get_torrent_contexts(); });
    widget->m_torrents_table_view = main_splitter.add<GUI::TableView>();
    widget->m_torrents_table_view->set_model(widget->m_torrent_model);
    widget->m_torrents_table_view->set_selection_mode(GUI::AbstractView::SelectionMode::MultiSelection);

    widget->m_torrents_table_view->on_context_menu_request = [widget, start_torrent_action, stop_torrent_action, cancel_checking_torrent_action](const GUI::ModelIndex& model_index, const GUI::ContextMenuEvent& event) {
        if (model_index.is_valid()) {
            widget->m_torrent_context_menu = GUI::Menu::construct();
            TorrentState state = widget->m_engine->torrents().at(model_index.row())->state();
            if (state == TorrentState::STOPPED || state == TorrentState::ERROR)
                widget->m_torrent_context_menu->add_action(start_torrent_action);
            else if (state == TorrentState::STARTED)
                widget->m_torrent_context_menu->add_action(stop_torrent_action);
            else if (state == TorrentState::CHECKING)
                widget->m_torrent_context_menu->add_action(cancel_checking_torrent_action);

            widget->m_torrent_context_menu->popup(event.screen_position());
        }
    };

    widget->m_bottom_tab_widget = main_splitter.add<GUI::TabWidget>();
    widget->m_bottom_tab_widget->set_preferred_height(14);
    widget->m_peer_list_widget = widget->m_bottom_tab_widget->add_tab<PeersTabWidget>("Peers"_string.release_value(), [widget] {
        int selected_index = widget->m_torrents_table_view->selection().first().row();
        if (selected_index < 0) {
            return Optional<NonnullRefPtr<Data::TorrentContext>> {};
        } else {

            return widget->m_engine->get_torrent_context(widget->m_engine->torrents().at(selected_index)->meta_info().info_hash());
        }
    });

    widget->m_torrents_table_view->on_selection_change = [widget] {
        Core::EventLoop::current().post_event(*widget->m_peer_list_widget, make<Core::CustomEvent>(BitsUiEvents::TorrentSelected));
    };

    widget->m_update_timer = widget->add<Core::Timer>(
        500, [widget] {
            widget->m_torrent_model->update();
            widget->m_peer_list_widget->refresh();

            u64 progress = 0;
            for (auto const& torrent : widget->m_engine->get_torrent_contexts()) {
                progress += torrent->local_bitfield.progress();
            }
            warn("\033]9;{};{};\033\\", progress, widget->m_engine->get_torrent_contexts().size() * 100);
        });
    widget->m_update_timer->start();

    return widget;
}

BitsWidget::BitsWidget(NonnullRefPtr<Engine> engine)
    : m_engine(move(engine))
{
}

}