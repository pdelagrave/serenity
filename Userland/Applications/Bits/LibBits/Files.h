/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/DeprecatedString.h>

namespace Bits {

ErrorOr<void> create_file_with_subdirs(const AK::DeprecatedString& absolute_path);

class File : public RefCounted<File> {
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

class LocalFile : public RefCounted<LocalFile> {
public:
    LocalFile(DeprecatedString local_path, NonnullRefPtr<File> meta_info_file)
        : m_meta_info_file(move(meta_info_file))
        , m_local_path(move(local_path))
    {
    }

    NonnullRefPtr<File> meta_info_file() { return m_meta_info_file; };
    DeprecatedString local_path() { return m_local_path; };

private:
    NonnullRefPtr<File> m_meta_info_file;
    DeprecatedString m_local_path;
};
}