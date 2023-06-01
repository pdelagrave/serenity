/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "BitsWidget.h"
#include "Engine.h"
#include <LibCore/ArgsParser.h>
#include <LibCore/System.h>
#include <LibFileSystemAccessClient/Client.h>
#include <LibGUI/Application.h>
#include <LibGUI/Icon.h>
#include <LibGUI/Menubar.h>
#include <LibMain/Main.h>

ErrorOr<int> serenity_main(Main::Arguments arguments)
{
    TRY(Core::System::pledge("stdio unix recvfd rpath sendfd inet wpath cpath thread"));

    bool skip_checking = false;
    bool assume_valid_when_skip_checking = false;
    Vector<StringView> paths;

    Core::ArgsParser args_parser;
    args_parser.set_general_help("A BitTorrent client");
    args_parser.add_option(skip_checking, "Skip checking existing files validity when adding a torrent. Data will be assumed to be invalid and pieces will be downloaded.", "skip-checking", 's');
    args_parser.add_option(assume_valid_when_skip_checking, "When skipping checking existing file data when adding a torrent (-s), assume the data to be valid.", "assume-valid", 'V');
    args_parser.add_positional_argument(paths, "torrent files to add", "files", Core::ArgsParser::Required::No);
    args_parser.parse(arguments);

    auto app = TRY(GUI::Application::create(arguments));

    auto app_icon = TRY(GUI::Icon::try_create_default_icon("hard-disk"sv));

    auto window = TRY(GUI::Window::try_create());
    window->set_title("Bits");
    window->resize(640, 400);

    auto engine = TRY(Bits::Engine::try_create(skip_checking, assume_valid_when_skip_checking));
    auto bits_widget = TRY(window->set_main_widget<Bits::BitsWidget>(engine));

    auto& file_menu = window->add_menu("&File"_string.release_value());
    file_menu.add_action(GUI::CommonActions::make_open_action([&window, &bits_widget](auto&) {
        auto x = FileSystemAccessClient::Client::the().open_file(window, {}, Core::StandardPaths::home_directory(), Core::File::OpenMode::Read);
        bits_widget->open_file(x.value().filename(), x.value().release_stream()).release_value();
    }));

    file_menu.add_action(GUI::CommonActions::make_quit_action([&](auto&) {
        window->close();
        app->quit();
    }));

    window->show();
    window->set_icon(app_icon.bitmap_for_size(16));

    if (!paths.is_empty()) {
        for (auto& path : paths) {
            auto response = FileSystemAccessClient::Client::the().request_file_read_only_approved(window, path);
            if (response.is_error())
                return 1;
            TRY(bits_widget->open_file(response.value().filename(), response.value().release_stream()));
        }
    }

    return app->exec();
}