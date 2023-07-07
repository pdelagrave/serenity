/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "BitsWidget.h"
#include <LibCore/ArgsParser.h>
#include <LibCore/System.h>
#include <LibFileSystemAccessClient/Client.h>
#include <LibGUI/Application.h>
#include <LibGUI/Icon.h>
#include <LibGUI/Menubar.h>
#include <LibMain/Main.h>

ErrorOr<int> serenity_main(Main::Arguments arguments)
{
    TRY(Core::System::pledge("stdio unix recvfd rpath sendfd inet wpath cpath thread accept"));

    bool start_cmd_line_torrent = false;

    u64 max_total_connections = Bits::Configuration::DEFAULT_MAX_TOTAL_CONNECTIONS;
    u64 max_connections_per_torrent = Bits::Configuration::DEFAULT_MAX_CONNECTIONS_PER_TORRENT;
    Vector<StringView> paths;

    Core::ArgsParser args_parser;
    args_parser.set_general_help("A BitTorrent client");

    auto total_help_str = DeprecatedString::formatted("Maximum number of connections in total [{}]", max_total_connections);
    args_parser.add_option(max_total_connections, total_help_str.characters(), "max-total-connections", 'm', "uint");

    auto per_torrent_help_str = DeprecatedString::formatted("Maximum number of connections per torrent [{}]", max_connections_per_torrent);
    args_parser.add_option(max_connections_per_torrent, per_torrent_help_str.characters(), "max-connections-per-torrent", 't', "uint");

    args_parser.add_option(start_cmd_line_torrent, "Start the torrents specified on the command line", "start", 's');
    args_parser.add_positional_argument(paths, "torrent files to open", "files", Core::ArgsParser::Required::No);
    args_parser.parse(arguments);

    auto app = TRY(GUI::Application::create(arguments));
    auto app_icon = TRY(GUI::Icon::try_create_default_icon("hard-disk"sv));

    auto window = TRY(GUI::Window::try_create());
    window->set_title("Bits");
    window->resize(800, 600);

    auto engine = TRY(Bits::Engine::try_create(Bits::Configuration(max_total_connections, max_connections_per_torrent)));

    auto bits_widget = TRY(BitsWidget::create(engine, window));
    window->set_main_widget(bits_widget);
    window->set_icon(app_icon.bitmap_for_size(16));
    window->show();

    if (!paths.is_empty()) {
        for (auto& path : paths) {
            auto response = FileSystemAccessClient::Client::the().request_file_read_only_approved(window, path);
            if (response.is_error())
                return 1;
            bits_widget->open_file(response.value().filename(), response.value().release_stream(), start_cmd_line_torrent);
        }
    }

    return app->exec();
}