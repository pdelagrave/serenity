/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "GetBitsWidget.h"
#include "MetaInfo.h"
#include <Applications/GetBits/GetBitsWindowGML.h>
#include <LibFileSystemAccessClient/Client.h>
#include <LibGUI/Action.h>
#include <LibGUI/Menu.h>
#include <LibGUI/Toolbar.h>

ErrorOr<void> GetBitsWidget::open_file(String const& filename, NonnullOwnPtr<Core::File> file)
{
    dbgln("Opening file {}", filename);
    auto meta_info = TRY(MetaInfo::create(*file));
    dbgln("Announce url: {}", meta_info.announce());
    return {};
}

void GetBitsWidget::initialize_menubar(GUI::Window& window)
{
    auto& file_menu = window.add_menu("&File");
    file_menu.add_action(*m_open_action);
}

GetBitsWidget::GetBitsWidget()
{
    load_from_gml(get_bits_window_gml).release_value_but_fixme_should_propagate_errors();

    m_toolbar = *find_descendant_of_type_named<GUI::Toolbar>("toolbar");

    m_open_action = GUI::CommonActions::make_open_action([this](auto&) {
        auto response = FileSystemAccessClient::Client::the().open_file(window(), {}, Core::StandardPaths::home_directory(), Core::File::OpenMode::Read);
        if (response.is_error())
            return;

        open_file(response.value().filename(), response.value().release_stream()).release_error();
    });

    m_toolbar->add_action(*m_open_action);
}
