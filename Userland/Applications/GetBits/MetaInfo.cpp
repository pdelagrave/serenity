/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "MetaInfo.h"
#include "BDecoder.h"

ErrorOr<MetaInfo> MetaInfo::create(SeekableStream& stream)
{
    auto meta_info = new MetaInfo();
    auto root = TRY(BDecoder::parse_bencoded(stream)).get<bencoded_dict>();
    meta_info->m_announce = TRY(String::from_utf8(StringView(root.get("announce"_string.release_value_but_fixme_should_propagate_errors()).value().get<ByteBuffer>().bytes())));
    return *meta_info;
}