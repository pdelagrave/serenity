/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "BDecoder.h"
#include <AK/DeprecatedString.h>
#include <AK/Stream.h>
#include <AK/URL.h>

namespace Bits {

class MetaInfo {
public:
    static ErrorOr<MetaInfo*> create(Stream&);
    URL announce() { return m_announce; };
    u8 const* info_hash() const { return m_info_hash; }
    i64 piece_length() { return m_piece_length; }
    i64 length() { return m_length; }
    i64 last_piece_length();
    DeprecatedString filename() { return m_file; }

private:
    MetaInfo() {};
    URL m_announce;
    u8 m_info_hash[20];
    i64 m_piece_length;
    i64 m_length;
    DeprecatedString m_file;
};
}