/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "TorrentDataFileMap.h"
#include "BitField.h"
#include "LibCrypto/Hash/HashManager.h"

namespace Bits {

NonnullOwnPtr<MultiFileMapperStream> MultiFileMapperStream::create(NonnullOwnPtr<Vector<NonnullRefPtr<Bits::LocalFile>>> files)
{
    size_t i = 0;
    u64 total_length = 0;
    auto files_positions = make<Vector<NonnullRefPtr<MappedFilePosition>>>();
    for (auto file : *files) {
        total_length += file->meta_info_file()->length();

        auto optional_fs_file = Optional<NonnullOwnPtr<SeekableStream>>();
        optional_fs_file.lazy_emplace([&file] { return Core::InputBufferedFile::create(Core::File::open(file->local_path(), Core::File::OpenMode::ReadWrite).release_value()).release_value(); });
        auto mapped_file_position = make_ref_counted<MappedFilePosition>(i, total_length, move(optional_fs_file));

        files_positions->append(move(mapped_file_position));
    }
    return adopt_nonnull_own_or_enomem(new (nothrow) MultiFileMapperStream(move(files_positions), total_length)).release_value_but_fixme_should_propagate_errors();
}

MultiFileMapperStream::~MultiFileMapperStream()
{
    close();
}

ErrorOr<void> MultiFileMapperStream::read_until_filled(Bytes buffer)
{
    size_t nread = 0;

    while (nread < buffer.size()) {
        if (current_fs_file().is_eof()) {
            if (m_current_file->file_index == m_files_positions->size() - 1)
                return Error::from_string_view_or_print_error_and_return_errno("Reached end-of-file before filling the entire buffer"sv, EIO);

            m_current_file = m_files_positions->at(m_current_file->file_index + 1);
            TRY(current_fs_file().seek(0, SeekMode::SetPosition));
        }

        auto result = read_some(buffer.slice(nread));
        if (result.is_error()) {
            if (result.error().is_errno() && result.error().code() == EINTR) {
                continue;
            }

            return result.release_error();
        }

        m_current_offset += result.value().size();
        nread += result.value().size();
    }

    return {};
}

ErrorOr<size_t> MultiFileMapperStream::seek(i64 offset, SeekMode mode)
{
    VERIFY(mode == SeekMode::SetPosition);

    if ((u64)offset == m_current_offset)
        return offset;


    auto* mapped_file_position = m_files_positions_by_offset.find_smallest_not_below(offset);
    if (!mapped_file_position)
        return Error::from_string_literal("Invalid offset");

    m_current_file = *mapped_file_position;
    u64 prev_relative_zero_offset = m_current_file->file_index == 0 ? 0 : m_files_positions->at(m_current_file->file_index - 1)->relative_zero_offset;
    dbgln("m_current_file->file_index: {}, prev_relative_zero_offset: {}", m_current_file->file_index, prev_relative_zero_offset);
    TRY(m_current_file->fs_file.value()->seek(offset - prev_relative_zero_offset, SeekMode::SetPosition));
    m_current_offset = (u64) offset;

    return offset;
}

ErrorOr<void> MultiFileMapperStream::truncate(size_t)
{
    VERIFY_NOT_REACHED();
}
ErrorOr<size_t> MultiFileMapperStream::tell() const
{
    VERIFY_NOT_REACHED();
}
ErrorOr<size_t> MultiFileMapperStream::size()
{
    return m_total_length;
}
ErrorOr<void> MultiFileMapperStream::discard(size_t)
{
    VERIFY_NOT_REACHED();
}
ErrorOr<Bytes> MultiFileMapperStream::read_some(Bytes bytes)
{
    return current_fs_file().read_some(bytes);
}
ErrorOr<ByteBuffer> MultiFileMapperStream::read_until_eof(size_t)
{
    VERIFY_NOT_REACHED();
}
ErrorOr<size_t> MultiFileMapperStream::write_some(ReadonlyBytes bytes)
{
    return current_fs_file().write_some(bytes);
}

// TODO test writing a piece that spans multiple files
ErrorOr<void> MultiFileMapperStream::write_until_depleted(ReadonlyBytes buffer)
{
    size_t nwritten = 0;
    while (nwritten < buffer.size()) {
        if (current_fs_file().is_eof()) {
            dbgln("Writing to file but the current one is eof {}, moving to {}", m_current_file->file_index, m_current_file->file_index + 1);
            if (m_current_file->file_index == m_files_positions->size() - 1)
                return Error::from_string_view_or_print_error_and_return_errno("Reached end-of-file before filling the entire buffer"sv, EIO);

            m_current_file = m_files_positions->at(m_current_file->file_index + 1);
            TRY(current_fs_file().seek(0, SeekMode::SetPosition));
        }

        auto result = write_some(buffer.slice(nwritten));
        if (result.is_error()) {
            if (result.error().is_errno() && result.error().code() == EINTR) {
                continue;
            }

            return result.release_error();
        }

        m_current_offset += result.value();
        nwritten += result.value();
    }
    dbgln("wrote {} bytes in total", nwritten);

    return {};
}
bool MultiFileMapperStream::is_eof() const
{
    VERIFY_NOT_REACHED();
}
bool MultiFileMapperStream::is_open() const
{
    VERIFY_NOT_REACHED();
}
void MultiFileMapperStream::close()
{
    for (auto fp : *m_files_positions) {
        if (fp->fs_file.has_value())
            fp->fs_file.value()->close();
    }
}

TorrentDataFileMap::TorrentDataFileMap(ByteBuffer piece_hashes, i64 piece_length, NonnullOwnPtr<Vector<NonnullRefPtr<LocalFile>>> files)
    : m_piece_hashes(piece_hashes)
    , m_piece_length(piece_length)
    , m_files_mapper(MultiFileMapperStream::create(move(files)))
{
}

ErrorOr<bool> TorrentDataFileMap::write_piece(u32 index, ReadonlyBytes data)
{
    TRY(m_files_mapper->seek(index * m_piece_length, SeekMode::SetPosition));
    TRY(m_files_mapper->write_until_depleted(data));
    return true;
}

ErrorOr<bool> TorrentDataFileMap::check_piece(i64 index, bool is_last_piece)
{
    auto piece_length = is_last_piece ? m_files_mapper->total_length() % m_piece_length : m_piece_length;
    TRY(m_files_mapper->seek(index * m_piece_length, SeekMode::SetPosition));
    auto piece_data = TRY(ByteBuffer::create_zeroed(piece_length));
    TRY(m_files_mapper->read_until_filled(piece_data.bytes()));

    return validate_hash(index, piece_data.bytes().slice(0, piece_length));
}

ErrorOr<bool> TorrentDataFileMap::validate_hash(i64 index, AK::ReadonlyBytes data)
{
    auto piece_hash = TRY(m_piece_hashes.slice(index * 20, 20));
    m_sha1.update(data); // not thread safe, this will cause problem.
    return m_sha1.digest().bytes() == piece_hash.bytes();
}

}