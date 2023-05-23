/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "MetaInfo.h"
#include "BDecoder.h"
#include "BEncoder.h"
#include <LibCrypto/Hash/HashManager.h>

namespace Bits {

ErrorOr<NonnullOwnPtr<MetaInfo>> MetaInfo::create(Stream& stream)
{
    auto meta_info = TRY(adopt_nonnull_own_or_enomem(new (nothrow) MetaInfo()));
    auto root = TRY(BDecoder::parse<Dict>(stream));

    // TODO: support tracker-less torrent (DHT), some torrent files have no announce url.
    meta_info->m_announce = URL(root.get_string("announce"));
    if (!meta_info->m_announce.is_valid()) {
        return Error::from_string_view(TRY(String::formatted("'{}' is not a valid URL", meta_info->m_announce)).bytes_as_string_view());
    }

    auto info_dict = root.get<Dict>("info");

    auto s1 = AllocatingMemoryStream();

    TRY(BEncoder::bencode(info_dict, s1));
    size_t buffer_size = s1.used_buffer_size();
    auto buffer = TRY(ByteBuffer::create_uninitialized(buffer_size));
    TRY(s1.read_until_filled(buffer.bytes()));

    Crypto::Hash::Manager hash;
    hash.initialize(Crypto::Hash::HashKind::SHA1);
    hash.update(buffer.bytes().slice(0, buffer_size));
    memcpy(meta_info->m_info_hash, hash.digest().immutable_data(), 20);

    meta_info->m_piece_length = info_dict.get<i64>("piece length");
    if (info_dict.contains("length")) {
        // single file mode
        meta_info->m_files.append(File(info_dict.get_string("name"), info_dict.get<i64>("length")));
        meta_info->m_total_length = info_dict.get<i64>("length");
    } else {
        // multi file mode
        meta_info->m_root_dir_name = info_dict.get_string("name");
        auto files = info_dict.get<List>("files");
        for (auto& file : files) {
            auto file_dict = file.get<Dict>();
            auto path = file_dict.get<List>("path");
            StringBuilder path_builder;
            for (auto path_element : path) {
                path_builder.append(TRY(DeprecatedString::from_utf8(path_element.get<ByteBuffer>().bytes())));
                path_builder.append('/');
            }
            path_builder.trim(1);
            i64 length = file_dict.get<i64>("length");
            meta_info->m_files.append(File(path_builder.to_deprecated_string(), length));
            meta_info->m_total_length += length;
            dbgln("path: {}, length: {}, totallength: {}", path_builder.to_string().release_value(), length, meta_info->m_total_length);
        }
    }

    return meta_info;
}
i64 MetaInfo::total_length()
{
    return m_total_length;
}

}