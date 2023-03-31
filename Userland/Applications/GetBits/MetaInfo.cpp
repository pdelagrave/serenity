/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "MetaInfo.h"
#include "BDecoder.h"
#include "BEncoder.h"
#include <AK/MemoryStream.h>
#include <LibCrypto/Hash/HashManager.h>

ErrorOr<MetaInfo> MetaInfo::create(Stream& stream)
{
    auto meta_info = new MetaInfo();
    auto root = TRY(BDecoder::parse_bencoded(stream)).get<bencoded_dict>();

    meta_info->m_announce = URL(TRY(String::from_utf8(StringView(root.get("announce"_string.release_value_but_fixme_should_propagate_errors()).value().get<ByteBuffer>().bytes()))));
    if (!meta_info->m_announce.is_valid()) {
        return Error::from_string_view(TRY(String::formatted("'{}' is not a valid URL", meta_info->m_announce.to_deprecated_string())).to_deprecated_string());
    }

    auto info_dict = root.get("info"_string.release_value_but_fixme_should_propagate_errors()).value();

    auto s1 = AllocatingMemoryStream();

    TRY(BEncoder::bencode(info_dict, s1));
    size_t buffer_size = s1.used_buffer_size();
    auto buffer = TRY(ByteBuffer::create_uninitialized(buffer_size));
    TRY(s1.read_until_filled(buffer.bytes()));

    Crypto::Hash::Manager hash;
    hash.initialize(Crypto::Hash::HashKind::SHA1);
    hash.update(buffer.bytes().slice(0, buffer_size));
    memcpy(meta_info->m_info_hash, hash.digest().immutable_data(), 20);

    return *meta_info;
}