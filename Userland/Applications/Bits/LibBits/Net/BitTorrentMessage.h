/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "../FixedSizeByteString.h"
#include "AK/ByteBuffer.h"
#include "AK/DeprecatedString.h"
#include "AK/Endian.h"
#include "AK/MemoryStream.h"
#include "AK/TypeCasts.h"
#include "AK/Types.h"
#include "Userland/Applications/Bits/LibBits/BitField.h"
#include <initializer_list>

namespace Bits {

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
    virtual ~Message() = default;

    static DeprecatedString to_string(Type);
    virtual DeprecatedString to_string() const;

    u32 size() const { return serialized.size(); }

    ByteBuffer serialized;
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

    // Null message used only for keepalives
    Message()
        : serialized(ByteBuffer::create_uninitialized(0).release_value_but_fixme_should_propagate_errors())
        , type(Type::Choke) // Bogus value. Should never be used
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
    DeprecatedString to_string() const override;
};

class ChokeMessage : public Message {
public:
    ChokeMessage()
        : Message(Type::Choke, StreamWritable({}))
    {
    }
};

struct HandshakeMessage {
    u8 pstrlen;
    u8 pstr[19];
    u8 reserved[8];
    u8 info_hash[20];
    u8 peer_id[20];

    HandshakeMessage(InfoHash info_hash, PeerId peer_id)
        : pstrlen(19)
    {
        memcpy(pstr, "BitTorrent protocol", 19);
        memset(reserved, 0, 8);
        memcpy(this->info_hash, info_hash.bytes().data(), 20);
        memcpy(this->peer_id, peer_id.bytes().data(), 20);
    }

    HandshakeMessage() = default;

    static ErrorOr<NonnullOwnPtr<HandshakeMessage>> try_create(Stream& stream)
    {
        auto handshake = new (nothrow) HandshakeMessage();
        TRY(stream.read_until_filled(Bytes { handshake, sizeof(HandshakeMessage) }));
        return adopt_nonnull_own_or_enomem(handshake);
    }

    DeprecatedString to_string() const;
};

class HaveMessage : public Message {
public:
    HaveMessage(BigEndian<u32> piece_index)
        : Message(Type::Have, piece_index)
        , piece_index(piece_index)
    {
    }

    HaveMessage(SeekableStream& stream)
        : Message(stream)
        , piece_index(stream.read_value<BigEndian<u32>>().release_value_but_fixme_should_propagate_errors())
    {
    }

    BigEndian<u32> piece_index;
    DeprecatedString to_string() const override;
};

class InterestedMessage : public Message {
public:
    InterestedMessage()
        : Message(Type::Interested, StreamWritable({}))
    {
    }
};

class KeepAliveMessage : public Message {
};

class NotInterestedMessage : public Message {
public:
    NotInterestedMessage()
        : Message(Type::NotInterested, StreamWritable({}))
    {
    }
};

class PieceMessage : public Message {
public:
    PieceMessage(BigEndian<u32> piece_index, BigEndian<u32> begin_offset, ByteBuffer block)
        : Message(Type::Piece, piece_index, begin_offset)
        , piece_index(piece_index)
        , begin_offset(begin_offset)
        , block(block)
    {
        serialized.append(block);
    }

    PieceMessage(SeekableStream& stream)
        : Message(stream)
        , piece_index(stream.read_value<BigEndian<u32>>().release_value_but_fixme_should_propagate_errors())
        , begin_offset(stream.read_value<BigEndian<u32>>().release_value_but_fixme_should_propagate_errors())
        , block(stream.read_until_eof().release_value_but_fixme_should_propagate_errors())
    {
    }

    BigEndian<u32> const piece_index;
    BigEndian<u32> const begin_offset;
    const ByteBuffer block;
    DeprecatedString to_string() const override;
};

class RequestMessage : public Message {
public:
    RequestMessage(BigEndian<u32> piece_index, BigEndian<u32> piece_offset, BigEndian<u32> block_length)
        : Message(Type::Request, piece_index, piece_offset, block_length)
        , piece_index(piece_index)
        , piece_offset(piece_offset)
        , block_length(block_length)
    {
    }

    RequestMessage(SeekableStream& stream)
        : Message(stream)
        , piece_index(stream.read_value<BigEndian<u32>>().release_value_but_fixme_should_propagate_errors())
        , piece_offset(stream.read_value<BigEndian<u32>>().release_value_but_fixme_should_propagate_errors())
        , block_length(stream.read_value<BigEndian<u32>>().release_value_but_fixme_should_propagate_errors())
    {
    }

    BigEndian<u32> const piece_index;
    BigEndian<u32> const piece_offset;
    BigEndian<u32> const block_length;
    DeprecatedString to_string() const override;
};

class UnchokeMessage : public Message {
public:
    UnchokeMessage()
        : Message(Type::Unchoke, StreamWritable({}))
    {
    }
};

}

template<>
struct AK::Formatter<Bits::Message> : AK::Formatter<StringView> {
    ErrorOr<void> format(FormatBuilder& builder, Bits::Message const& value)
    {
        return Formatter<StringView>::format(builder, value.to_string());
    }
};