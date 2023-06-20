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

    bool skip_checking = false;
    bool assume_valid_when_skip_checking = false;
    bool start_cmd_line_torrent = false;
    Vector<StringView> paths;

    Core::ArgsParser args_parser;
    args_parser.set_general_help("A BitTorrent client");
    args_parser.add_option(skip_checking, "Skip checking existing files validity when adding a torrent. Data will be assumed to be invalid and pieces will be downloaded.", "skip-checking", 's');
    args_parser.add_option(assume_valid_when_skip_checking, "When skipping checking existing file data when adding a torrent (-s), assume the data to be valid.", "assume-valid", 'V');
    args_parser.add_option(start_cmd_line_torrent, "Start the torrent specified on the command line.", "start", 'S');
    args_parser.add_positional_argument(paths, "torrent files to add", "files", Core::ArgsParser::Required::No);
    args_parser.parse(arguments);

    auto app = TRY(GUI::Application::create(arguments));
    auto app_icon = TRY(GUI::Icon::try_create_default_icon("hard-disk"sv));

    auto window = TRY(GUI::Window::try_create());
    window->set_title("Bits");
    window->resize(800, 600);

    auto engine = TRY(Bits::Engine::try_create(skip_checking, assume_valid_when_skip_checking));
    Bits::Engine::s_engine = engine.ptr();

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