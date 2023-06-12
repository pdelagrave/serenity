/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../BitField.h"
#include "AK/MemoryStream.h"
#include <AK/ByteBuffer.h>
#include <AK/DeprecatedString.h>
#include <AK/Endian.h>
#include <AK/TypeCasts.h>
#include <AK/Types.h>
#include <initializer_list>

namespace Bits::BitTorrent {

class Message {
public:
    enum class Type : u8 {
        Choke = 0x00,
        Unchoke = 0x01,
        Interested = 0x02,
        NotInterested = 0x03,
        Have = 0x04,
        Bitfield = 0x05,
        Request = 0x06,
        Piece = 0x07,
        Cancel = 0x08
    };
    static DeprecatedString to_string(Type);

    u32 size() const { return serialized.size(); }

    const ByteBuffer serialized;
    const Type type;

protected:
    class StreamWritable {
    public:
        StreamWritable(ReadonlyBytes bytes)
            : m_bytes(bytes)
        {
        }

        ErrorOr<void> write_to_stream(AK::Stream& stream) const
        {
            return stream.write_until_depleted(m_bytes);
        }

    private:
        ReadonlyBytes m_bytes;
    };

    template<typename... Payload>
    Message(Type type, Payload... payloads)
        : serialized(serialize(type, payloads...))
        , type(type)
    {
    }

    Message(SeekableStream& stream)
        : serialized(copy_already_serialized(stream))
        , type(stream.read_value<Type>().release_value_but_fixme_should_propagate_errors())
    {
    }

private:
    // TODO: make this variadic template argument optional so we don't have to use StreamWritable({}) for messages with no payload
    template<typename... Payload>
    static ByteBuffer serialize(Type message_type, Payload... payloads)
    {
        auto stream = AllocatingMemoryStream();

        stream.write_value(static_cast<u8>(message_type)).release_value_but_fixme_should_propagate_errors();
        for (auto const& param : { payloads... }) {
            stream.write_value(param).release_value_but_fixme_should_propagate_errors();
        }
        auto buffer = ByteBuffer::create_zeroed(stream.used_buffer_size()).release_value_but_fixme_should_propagate_errors();
        stream.read_until_filled(buffer.bytes()).release_value_but_fixme_should_propagate_errors();
        return buffer;
    }

    static ByteBuffer copy_already_serialized(SeekableStream& stream)
    {
        auto buffer = stream.read_until_eof().release_value_but_fixme_should_propagate_errors();
        stream.seek(0, AK::SeekMode::SetPosition).release_value_but_fixme_should_propagate_errors();
        return buffer;
    }
};

class BitFieldMessage : public Message {
public:
    BitFieldMessage(BitField bitfield)
        : Message(Type::Bitfield, bitfield)
        , bitfield(bitfield)
    {
    }

    BitFieldMessage(SeekableStream& stream)
        : Message(stream)
        , bitfield(stream.read_value<BitField>().release_value_but_fixme_should_propagate_errors())
    {
    }
    const BitField bitfield;
};

class Choke : public Message {
public:
    Choke()
        : Message(Type::Choke, StreamWritable({}))
    {
    }
};

struct Handshake {
    u8 pstrlen;
    u8 pstr[19];
    u8 reserved[8];
    u8 info_hash[20];
    u8 peer_id[20];

    Handshake(ReadonlyBytes info_hash, ReadonlyBytes peer_id)
        : pstrlen(19)
    {
        VERIFY(info_hash.size() == 20);
        VERIFY(peer_id.size() == 20);
        memcpy(pstr, "BitTorrent protocol", 19);
        memset(reserved, 0, 8);
        memcpy(this->info_hash, info_hash.data(), 20);
        memcpy(this->peer_id, peer_id.data(), 20);
    }

    Handshake() = default;

    static ErrorOr<NonnullOwnPtr<Handshake>> try_create(Stream& stream)
    {
        auto handshake = new (nothrow) Handshake();
        TRY(stream.read_until_filled(Bytes { handshake, sizeof(Handshake) }));
        return adopt_nonnull_own_or_enomem(handshake);
    }
};

class Have : public Message {
public:
    Have(BigEndian<u32> piece_index)
        : Message(Type::Have, piece_index)
        , piece_index(piece_index)
    {
    }

    Have(SeekableStream& stream)
        : Message(stream)
        , piece_index(stream.read_value<BigEndian<u32>>().release_value_but_fixme_should_propagate_errors())
    {
    }

    BigEndian<u32> piece_index;
};

class Interested : public Message {
public:
    Interested()
        : Message(Type::Interested, StreamWritable({}))
    {
    }
};

class NotInterested : public Message {
public:
    NotInterested()
        : Message(Type::NotInterested, StreamWritable({}))
    {
    }
};

class Piece : public Message {
public:
    Piece(SeekableStream& stream)
        : Message(stream)
        , piece_index(stream.read_value<BigEndian<u32>>().release_value_but_fixme_should_propagate_errors())
        , begin_offset(stream.read_value<BigEndian<u32>>().release_value_but_fixme_should_propagate_errors())
        , block(stream.read_until_eof().release_value_but_fixme_should_propagate_errors())
    {
    }

    BigEndian<u32> const piece_index;
    BigEndian<u32> const begin_offset;
    const ByteBuffer block;
};

class Request : public Message {
public:
    Request(BigEndian<u32> piece_index, BigEndian<u32> piece_offset, BigEndian<u32> block_length)
        : Message(Type::Request, piece_index, piece_offset, block_length)
        , piece_index(piece_index)
        , piece_offset(piece_offset)
        , block_length(block_length)
    {
    }
    BigEndian<u32> const piece_index;
    BigEndian<u32> const piece_offset;
    BigEndian<u32> const block_length;
};

class Unchoke : public Message {
public:
    Unchoke()
        : Message(Type::Unchoke, StreamWritable({}))
    {
    }
};

}

template<>
struct AK::Formatter<Bits::BitTorrent::Message> : AK::Formatter<StringView> {
    ErrorOr<void> format(FormatBuilder& builder, Bits::BitTorrent::Message const& value)
    {
        return Formatter<StringView>::format(builder, DeprecatedString::formatted("{} size: {}", Bits::BitTorrent::Message::to_string(value.type), value.size()));
    }
};