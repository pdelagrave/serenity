/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "BitsWidget.h"
#include <LibCore/System.h>
#include <LibFileSystemAccessClient/Client.h>
#include <LibGUI/Application.h>
#include <LibGUI/Icon.h>
#include <LibGUI/Menubar.h>
#include <LibMain/Main.h>

ErrorOr<int> serenity_main(Main::Arguments arguments)
{
    TRY(Core::System::pledge("stdio unix recvfd rpath sendfd inet wpath cpath thread"));

    auto app = TRY(GUI::Application::try_create(arguments));

    auto app_icon = TRY(GUI::Icon::try_create_default_icon("hard-disk"sv));

    auto window = TRY(GUI::Window::try_create());
    window->set_title("Bits");
    window->resize(640, 400);

    auto get_bits_widget = TRY(window->set_main_widget<Bits::BitsWidget>());

    get_bits_widget->initialize_menubar(*window);
    window->show();
    window->set_icon(app_icon.bitmap_for_size(16));

    if (arguments.argc > 1) {
        auto response = FileSystemAccessClient::Client::the().request_file(window, arguments.strings[1], Core::File::OpenMode::Read);
        if (response.is_error())
            return 1;
        TRY(get_bits_widget->open_file(response.value().filename(), response.value().release_stream()));
    }

    return app->exec();
}
