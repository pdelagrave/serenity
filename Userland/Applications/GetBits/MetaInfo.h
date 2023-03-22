/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Stream.h>
#include <AK/String.h>

class MetaInfo {
public:
    static ErrorOr<MetaInfo> create(SeekableStream&);
    String announce() { return m_announce; };
private:
    String m_announce;
};
