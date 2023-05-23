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

class File {
public:
    File(DeprecatedString path, i64 length)
        : m_path(path)
        , m_length(length) {};
    DeprecatedString path() { return m_path; };
    i64 length() { return m_length; };

private:
    const DeprecatedString m_path;
    const i64 m_length;
};

class MetaInfo {
public:
    static ErrorOr<NonnullOwnPtr<MetaInfo>> create(Stream&);
    URL announce() { return m_announce; };
    u8 const* info_hash() const { return m_info_hash; }
    i64 piece_length() { return m_piece_length; }
    Vector<File> files() { return m_files; }

    i64 total_length();

private:
    MetaInfo() {};
    URL m_announce;
    u8 m_info_hash[20];
    i64 m_piece_length;
    Vector<File> m_files;
    DeprecatedString m_root_dir_name;
    i64 m_total_length = 0;
};
}