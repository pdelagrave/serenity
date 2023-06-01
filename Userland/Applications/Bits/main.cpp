/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "BitsWidget.h"
#include "Engine.h"
#include <LibCore/System.h>
#include <LibFileSystemAccessClient/Client.h>
#include <LibGUI/Application.h>
#include <LibGUI/Icon.h>
#include <LibGUI/Menubar.h>
#include <LibMain/Main.h>

ErrorOr<int> serenity_main(Main::Arguments arguments)
{
    TRY(Core::System::pledge("stdio unix recvfd rpath sendfd inet wpath cpath thread"));

    auto app = TRY(GUI::Application::create(arguments));

    auto app_icon = TRY(GUI::Icon::try_create_default_icon("hard-disk"sv));

    auto window = TRY(GUI::Window::try_create());
    window->set_title("Bits");
    window->resize(640, 400);

    auto engine = TRY(Bits::Engine::try_create());
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

    if (arguments.argc > 1) {
        auto response = FileSystemAccessClient::Client::the().request_file(window, arguments.strings[1], Core::File::OpenMode::Read);
        if (response.is_error())
            return 1;
        TRY(bits_widget->open_file(response.value().filename(), response.value().release_stream()));
    }

    return app->exec();
}