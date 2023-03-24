/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "MetaInfo.h"
#include "BDecoder.h"
#include "BEncoder.h"
#include <AK/MemoryStream.h>
#include <AK/Base64.h>
#include <LibCrypto/Hash/HashManager.h>

ErrorOr<MetaInfo> MetaInfo::create(SeekableStream& stream)
{
    auto meta_info = new MetaInfo();
    auto root = TRY(BDecoder::parse_bencoded(stream)).get<bencoded_dict>();
    meta_info->m_announce = TRY(String::from_utf8(StringView(root.get("announce"_string.release_value_but_fixme_should_propagate_errors()).value().get<ByteBuffer>().bytes())));

    auto info_dict = root.get("info"_string.release_value_but_fixme_should_propagate_errors()).value();

    auto buffer = TRY(ByteBuffer::create_uninitialized(TRY(stream.tell())));
    auto s1 = FixedMemoryStream(buffer.span());
//
//    TRY(BEncoder::bencode(ByteBuffer::copy(TRY("hello!!"_string).bytes()).value(), s1));
//    TRY(BEncoder::bencode(345363, s1));
    TRY(BEncoder::bencode(info_dict, s1));
    auto encoded_size = TRY(s1.tell());
    dbgln("Size: {}", encoded_size);
    for (size_t i = 0; i < encoded_size; i++) {
        dbgln("{:c}", buffer[i]);
    }

    Crypto::Hash::Manager hash;
    hash.initialize(Crypto::Hash::HashKind::SHA1);
    hash.update(buffer.bytes().slice(0, encoded_size));
    auto expected_sha1 = hash.digest();
    auto expected_sha1_string = MUST(encode_base64({ expected_sha1.immutable_data(), expected_sha1.data_length() }));
    dbgln(expected_sha1_string);

    return *meta_info;
}