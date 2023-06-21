/*
 * Copyright (c) 2023, Pierre Delagrave <pierre.delagrave@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Forward.h>
#include <AK/Types.h>
#include <AK/Format.h>
#include <AK/DeprecatedString.h>
#include <AK/Span.h>

namespace Bits {

template<size_t size>
class FixedSizeByteString {
public:
    explicit FixedSizeByteString(const ReadonlyBytes &from_bytes) {
        VERIFY(from_bytes.size() == size);
        from_bytes.copy_to({m_data, size});
    }

    FixedSizeByteString(const FixedSizeByteString &other) {
        memcpy(m_data, other.m_data, size);
    }

    FixedSizeByteString() = delete;
    [[nodiscard]] ReadonlyBytes bytes() const {
        return {m_data, size};
    }

    [[nodiscard]] DeprecatedString to_escaped_ascii() const {
        StringBuilder sb;
        for (u8 c: m_data) {
            if (c >= 32 && c <= 126) {
                sb.append(c);
            } else {
                sb.appendff("\\x{:02X}", c);
            }
        }
        return sb.to_deprecated_string();
    }

    constexpr FixedSizeByteString& operator=(FixedSizeByteString const& other) = default;

    constexpr FixedSizeByteString& operator=(FixedSizeByteString&& other) = default;

    constexpr bool operator==(FixedSizeByteString const &other) const {
        return bytes() == other.bytes();
    }

    constexpr bool operator==(ReadonlyBytes const &other) const {
        return bytes() == other;
    }

private:
    u8 m_data[size] {};
};

using PeerId = FixedSizeByteString<20>;
using InfoHash = FixedSizeByteString<20>;

}

template<size_t size>
struct AK::Formatter<Bits::FixedSizeByteString<size>> : AK::Formatter<StringView> {
    ErrorOr<void> format(FormatBuilder &builder, Bits::FixedSizeByteString<size> const &value) {
        return Formatter<StringView>::format(builder, DeprecatedString::formatted("{}", value.to_escaped_ascii()));
    }
};

template<size_t size>
struct AK::Traits<Bits::FixedSizeByteString<size>> : public GenericTraits<Bits::FixedSizeByteString<size>> {
    static constexpr unsigned hash(Bits::FixedSizeByteString<size> const &string) {
        return AK::Traits<Span<const u8>>::hash(string.bytes());
    }
};
