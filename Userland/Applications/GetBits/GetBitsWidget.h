/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "LibGUI/Widget.h"

class GetBitsWidget final : public GUI::Widget {
    C_OBJECT(GetBitsWidget)
public:
    virtual ~GetBitsWidget() override = default;
    ErrorOr<void> open_file(String const& filename, NonnullOwnPtr<Core::File>);
    void initialize_menubar(GUI::Window&);

private:
    GetBitsWidget();

    RefPtr<GUI::Action> m_open_action;
    RefPtr<GUI::Toolbar> m_toolbar;
};