/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "BitsWidget.h"
#include "BitsUiEvents.h"
#include "PeersTabWidget.h"
#include <AK/NumberFormat.h>
#include <LibFileSystemAccessClient/Client.h>
#include <LibGUI/Action.h>
#include <LibGUI/Application.h>
#include <LibGUI/BoxLayout.h>
#include <LibGUI/Menu.h>
#include <LibGUI/MessageBox.h>
#include <LibGUI/SortingProxyModel.h>
#include <LibGUI/Splitter.h>

int TorrentModel::row_count(GUI::ModelIndex const&) const
{
    return m_torrents.size();
}
int TorrentModel::column_count(GUI::ModelIndex const&) const
{
    return Column::__Count;
}
GUI::Variant TorrentModel::data(GUI::ModelIndex const& index, GUI::ModelRole role) const
{
    if (role == GUI::ModelRole::TextAlignment)
        return Gfx::TextAlignment::CenterLeft;
    if (role == GUI::ModelRole::Display) {
        auto torrent = Bits::Engine::s_engine->torrents().at(index.row());
        Bits::MetaInfo& meta_info = torrent->meta_info();
        auto tcontext = m_torrents.at(index.row());

        switch (index.column()) {
        case Column::Name:
            return torrent->display_name();
        case Column::Size:
            return AK::human_readable_quantity(meta_info.total_length());
        case Column::State:
            return state_to_string(torrent->state()).release_value_but_fixme_should_propagate_errors();
        case Column::Progress:
            return DeprecatedString::formatted("{:.1}%", torrent->state() == Bits::TorrentState::CHECKING ? torrent->check_progress() : tcontext->local_bitfield.progress());
        case Column::DownloadSpeed:
            return DeprecatedString::formatted("{}/s", human_readable_size(tcontext->download_speed));
        case Column::UploadSpeed:
            return DeprecatedString::formatted("{}/s", human_readable_size(tcontext->upload_speed));
        case Column::Path:
            return torrent->data_path();
        default:
            VERIFY_NOT_REACHED();
        }
    }
    return {};
}

void TorrentModel::update()
{
    m_torrents = m_get_updated_torrent_list();
    did_update(UpdateFlag::DontInvalidateIndices);
}
String TorrentModel::column_name(int column) const
{
    switch (column) {
    case Column::Name:
        return "Name"_short_string;
    case Column::Size:
        return "Size"_short_string;
    case Column::State:
        return "State"_string.release_value();
    case Column::Progress:
        return "Progress"_string.release_value();
    case Column::DownloadSpeed:
        return "Download Speed"_string.release_value();
    case Column::UploadSpeed:
        return "Upload Speed"_string.release_value();
    case Column::Path:
        return "Path"_string.release_value();
    default:
        VERIFY_NOT_REACHED();
    }
}

void BitsWidget::open_file(String const& filename, NonnullOwnPtr<Core::File> file, bool start)
{
    dbgln("Opening file {}", filename);
    auto maybe_meta_info = Bits::MetaInfo::create(*file);
    file->close();

    if (maybe_meta_info.is_error()) {
        GUI::MessageBox::show_error(this->window(), "Error opening file"sv);
        return;
    }

    m_engine->add_torrent(maybe_meta_info.release_value(), Core::StandardPaths::downloads_directory());

    if (start)
        m_engine->start_torrent(m_engine->torrents().size() - 1);

    if (m_engine->torrents().size() > 0)
        m_torrents_table_view->selection().set(m_torrents_table_view->model()->index(0, 0));

    return;
}

ErrorOr<NonnullRefPtr<BitsWidget>> BitsWidget::create(NonnullRefPtr<Bits::Engine> engine, GUI::Window* window)
{
    auto widget = TRY(adopt_nonnull_ref_or_enomem(new (nothrow) BitsWidget(move(engine))));

    widget->set_layout<GUI::VerticalBoxLayout>();

    auto& file_menu = window->add_menu("&File"_string.release_value());

    file_menu.add_action(GUI::CommonActions::make_open_action([window, widget](auto&) {
        FileSystemAccessClient::OpenFileOptions options {
            .window_title = "Open a torrent file"sv,
            .path = Core::StandardPaths::home_directory(),
            .requested_access = Core::File::OpenMode::Read,
            .allowed_file_types = { { GUI::FileTypeFilter { "Torrent Files", { { "torrent" } } }, GUI::FileTypeFilter::all_files() } }
        };
        auto maybe_file = FileSystemAccessClient::Client::the().open_file(window, options);
        if (maybe_file.is_error()) {
            dbgln("err: {}", maybe_file.error());
            return;
        }

        widget->open_file(maybe_file.value().filename(), maybe_file.value().release_stream(), false);
    }));

    file_menu.add_action(GUI::CommonActions::make_quit_action([&](auto&) {
        GUI::Application::the()->quit();
    }));

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
            Bits::TorrentState state = widget->m_engine->torrents().at(model_index.row())->state();
            if (state == Bits::TorrentState::STOPPED || state == Bits::TorrentState::ERROR)
                widget->m_torrent_context_menu->add_action(start_torrent_action);
            else if (state == Bits::TorrentState::STARTED)
                widget->m_torrent_context_menu->add_action(stop_torrent_action);
            else if (state == Bits::TorrentState::CHECKING)
                widget->m_torrent_context_menu->add_action(cancel_checking_torrent_action);

            widget->m_torrent_context_menu->popup(event.screen_position());
        }
    };

    widget->m_bottom_tab_widget = main_splitter.add<GUI::TabWidget>();
    widget->m_bottom_tab_widget->set_preferred_height(14);
    widget->m_peer_list_widget = widget->m_bottom_tab_widget->add_tab<PeersTabWidget>("Peers"_string.release_value(), [widget] {
        int selected_index = widget->m_torrents_table_view->selection().first().row();
        if (selected_index < 0) {
            return Optional<NonnullRefPtr<Bits::TorrentContext>> {};
        } else {

            return widget->m_engine->get_torrent_context(widget->m_engine->torrents().at(selected_index)->meta_info().info_hash());
        }
    });

    widget->m_torrents_table_view->on_selection_change = [widget] {
        dbgln("SELECTION CHANGED!");
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

BitsWidget::BitsWidget(NonnullRefPtr<Bits::Engine> engine)
    : m_engine(move(engine))
{
}
