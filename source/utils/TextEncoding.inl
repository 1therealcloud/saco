/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * The original code is copyright (c) 2026, 1therealcloud.
 */

#pragma once

#include <Windows.h>
#include <algorithm>
#include <climits>
#include <string>
#include <string_view>

namespace text
{
    // UI text is UTF-8; legacy server text uses the Windows system code page.
    inline constexpr UINT CodePage = CP_UTF8;
    inline constexpr UINT ServerCodePage = CP_ACP;

    inline char32_t Decode(std::string_view value, std::size_t& position)
    {
        if (position >= value.size())
            return 0;
        const auto first = static_cast<unsigned char>(value[position++]);
        if (first < 0x80)
            return first;
        const unsigned length = first >= 0xC2 && first <= 0xDF ? 2 : first >= 0xE0 && first <= 0xEF ? 3 : first >= 0xF0 && first <= 0xF4 ? 4 : 0;
        if (!length || value.size() - position < length - 1)
            return 0xFFFD;
        char32_t result = first & (0x7F >> length);
        for (unsigned i = 1; i < length; ++i)
        {
            const auto next = static_cast<unsigned char>(value[position + i - 1]);
            if ((next & 0xC0) != 0x80)
                return 0xFFFD;
            result = (result << 6) | (next & 0x3F);
        }
        if ((length == 2 && result < 0x80) || (length == 3 && result < 0x800) || (length == 4 && result < 0x10000) || result > 0x10FFFF || (result >= 0xD800 && result <= 0xDFFF))
            return 0xFFFD;
        position += length - 1;
        return result;
    }

    inline std::size_t Boundary(std::string_view value, std::size_t bytes)
    {
        bytes = std::min(bytes, value.size());
        std::size_t position = 0;
        while (position < bytes)
        {
            const auto previous = position;
            const auto character = Decode(value, position);
            if (character == 0xFFFD && position == previous + 1 && static_cast<unsigned char>(value[previous]) >= 0x80)
                return previous;
            if (position > bytes)
                return previous;
        }
        return position;
    }

    inline std::size_t Count(std::string_view value)
    {
        std::size_t position = 0, count = 0;
        while (position < value.size())
        {
            Decode(value, position);
            ++count;
        }
        return count;
    }

    inline std::size_t Prefix(std::string_view value, std::size_t characters)
    {
        std::size_t position = 0;
        while (characters-- && position < value.size())
            Decode(value, position);
        return position;
    }

    inline std::wstring ToWide(std::string_view value, UINT codePage = CP_UTF8)
    {
        if (value.empty() || value.size() > INT_MAX)
            return {};
        if (codePage == CP_ACP)
            codePage = GetACP();
        const int length = MultiByteToWideChar(codePage, codePage == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
        std::wstring result(static_cast<std::size_t>(length), L'\0');
        if (length)
            MultiByteToWideChar(codePage, codePage == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0, value.data(), static_cast<int>(value.size()), result.data(), length);
        return result;
    }

    inline std::string FromWide(std::wstring_view value, UINT codePage = CP_UTF8)
    {
        if (value.empty() || value.size() > INT_MAX)
            return {};
        if (codePage == CP_ACP)
            codePage = GetACP();
        const DWORD flags = codePage == CP_UTF8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
        const int length = WideCharToMultiByte(codePage, flags, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<std::size_t>(length), '\0');
        if (length)
            WideCharToMultiByte(codePage, flags, value.data(), static_cast<int>(value.size()), result.data(), length, nullptr, nullptr);
        return result;
    }

    inline std::string FromServer(std::string_view value)
    {
        return FromWide(ToWide(value, ServerCodePage));
    }

    inline std::string ToServer(std::string_view value)
    {
        return FromWide(ToWide(value), ServerCodePage);
    }
}
