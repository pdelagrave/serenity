/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/DeprecatedString.h>
#include <AK/Format.h>
#include <AK/Forward.h>
#include <AK/Span.h>
#include <AK/Types.h>

namespace Bits {

template<size_t size>
class FixedSizeByteString {
public:
    explicit FixedSizeByteString(ReadonlyBytes const& from_bytes)
    {
        VERIFY(from_bytes.size() == size);
        from_bytes.copy_to({ m_data, size });
    }

    FixedSizeByteString(FixedSizeByteString const& other)
    {
        memcpy(m_data, other.m_data, size);
    }

    FixedSizeByteString() = delete;
    [[nodiscard]] ReadonlyBytes bytes() const
    {
        return { m_data, size };
    }

    constexpr FixedSizeByteString& operator=(FixedSizeByteString const& other) = default;

    constexpr FixedSizeByteString& operator=(FixedSizeByteString&& other) = default;

    constexpr bool operator==(FixedSizeByteString const& other) const
    {
        return bytes() == other.bytes();
    }

    constexpr bool operator==(ReadonlyBytes const& other) const
    {
        return bytes() == other;
    }

private:
    u8 m_data[size] {};
};

using PeerId = FixedSizeByteString<20>;
using InfoHash = FixedSizeByteString<20>;

}

template<size_t size>
struct AK::Formatter<Bits::FixedSizeByteString<size>> : AK::Formatter<FormatString> {
    ErrorOr<void> format(FormatBuilder& builder, Bits::FixedSizeByteString<size> const& value)
    {
        for (u8 c : value.bytes())
            TRY(Formatter<FormatString>::format(builder, "{:02X}"sv, c));
    }
};

template<>
struct AK::Formatter<Bits::PeerId> : AK::Formatter<FormatString> {
    ErrorOr<void> format(FormatBuilder& builder, Bits::PeerId const& value)
    {
        for (u8 c : value.bytes()) {
            if (c >= 32 && c <= 126)
                TRY(Formatter<FormatString>::format(builder, "{:c}"sv, c));
            else
                TRY(Formatter<FormatString>::format(builder, "\\x{:02X}"sv, c));
        }
        return {};
    }
};

template<size_t size>
struct AK::Traits<Bits::FixedSizeByteString<size>> : public GenericTraits<Bits::FixedSizeByteString<size>> {
    static constexpr unsigned hash(Bits::FixedSizeByteString<size> const& string)
    {
        return AK::Traits<Span<u8 const>>::hash(string.bytes());
    }
};
