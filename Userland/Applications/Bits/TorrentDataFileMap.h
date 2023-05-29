/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once
#include "BitField.h"
#include "Files.h"
#include "LibCore/File.h"
#include "LibCrypto/Hash/HashManager.h"
#include <AK/ByteBuffer.h>
#include <AK/Forward.h>
#include <AK/Optional.h>
#include <AK/RedBlackTree.h>
#include <AK/Vector.h>

namespace Bits {

class MultiFileMapperStream : public SeekableStream {
public:
    static NonnullOwnPtr<MultiFileMapperStream> create(NonnullOwnPtr<Vector<NonnullRefPtr<LocalFile>>> files);

    ErrorOr<void> read_until_filled(Bytes bytes) override;
    ErrorOr<size_t> seek(i64 offset, SeekMode mode) override;
    ErrorOr<void> truncate(size_t length) override;
    ErrorOr<size_t> tell() const override;
    ErrorOr<size_t> size() override;
    ErrorOr<void> discard(size_t discarded_bytes) override;
    ErrorOr<Bytes> read_some(Bytes bytes) override;
    ErrorOr<ByteBuffer> read_until_eof(size_t block_size) override;
    ErrorOr<size_t> write_some(ReadonlyBytes bytes) override;
    ErrorOr<void> write_until_depleted(ReadonlyBytes bytes) override;
    bool is_eof() const override;
    bool is_open() const override;
    void close() override;
    ~MultiFileMapperStream() override = default;

    u64 total_length() const { return m_total_length; }

private:
    struct MappedFilePosition : public RefCounted<MappedFilePosition> {
        MappedFilePosition(size_t file_index, i64 relative_zero_offset, Optional<NonnullOwnPtr<SeekableStream>> fs_file)
            : file_index(file_index)
            , relative_zero_offset(relative_zero_offset)
            , fs_file(move(fs_file))
        {
        }
        const size_t file_index;
        const i64 relative_zero_offset; // also used as the key of the BST
        Optional<NonnullOwnPtr<SeekableStream>> const fs_file;
    };

    MultiFileMapperStream(NonnullOwnPtr<Vector<NonnullRefPtr<MappedFilePosition>>> files_positions, u64 total_length)
        : m_current_file(files_positions->first())
        , m_files_positions(move(files_positions))
        , m_total_length(total_length)
    {
        for (auto mapped_file_position : *m_files_positions) {
            m_files_positions_by_offset.insert(mapped_file_position->relative_zero_offset, move(mapped_file_position));
        }
    }
    SeekableStream& current_fs_file()
    {
        return **m_current_file->fs_file;
    };

    NonnullRefPtr<MappedFilePosition> m_current_file;
    NonnullOwnPtr<Vector<NonnullRefPtr<MappedFilePosition>>> m_files_positions;
    RedBlackTree<u64, NonnullRefPtr<MappedFilePosition>> m_files_positions_by_offset;
    const u64 m_total_length;
    u64 m_current_offset { 0 };
};

class TorrentDataFileMap {
public:
    TorrentDataFileMap(ByteBuffer piece_hashes, i64 piece_length, NonnullOwnPtr<Vector<NonnullRefPtr<LocalFile>>> files);
    bool write_piece(u32 index, ByteBuffer const& data);
    ErrorOr<bool> check_piece(i64 index, bool is_last_piece);

private:
    ByteBuffer m_piece_hashes;
    i64 m_piece_length;
    NonnullOwnPtr<MultiFileMapperStream> m_files_mapper;
    Crypto::Hash::Manager m_sha1 { Crypto::Hash::HashKind::SHA1 };
};

}
