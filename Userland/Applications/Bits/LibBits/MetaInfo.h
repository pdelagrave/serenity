/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "AK/DeprecatedString.h"
#include "AK/Stream.h"
#include "AK/URL.h"
#include "Files.h"
#include "Userland/Applications/Bits/LibBits/Bencode/BDecoder.h"

namespace Bits {

class MetaInfo {
public:
    static ErrorOr<NonnullOwnPtr<MetaInfo>> create(Stream&);
    URL announce() { return m_announce; };
    ReadonlyBytes info_hash() const { return m_info_hash; }
    i64 piece_length() { return m_piece_length; }
    Vector<NonnullRefPtr<File>> files() { return m_files; }
    Optional<DeprecatedString> const& root_dir_name() const { return m_root_dir_name; }
    ByteBuffer piece_hashes() const { return m_piece_hashes; }

    i64 total_length();

private:
    MetaInfo() {};
    URL m_announce;
    ByteBuffer m_info_hash;
    i64 m_piece_length;
    ByteBuffer m_piece_hashes;
    Vector<NonnullRefPtr<File>> m_files;
    Optional<DeprecatedString> m_root_dir_name;
    i64 m_total_length = 0;
};
}