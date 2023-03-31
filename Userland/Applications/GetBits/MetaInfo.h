/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Stream.h>
#include <AK/String.h>
#include <AK/URL.h>

class MetaInfo {
public:
    static ErrorOr<MetaInfo> create(Stream&);
    URL announce() { return m_announce; };
    u8 const* info_hash() const { return m_info_hash; }

private:
    MetaInfo() {};
    URL m_announce;
    u8 m_info_hash[20];
};
