#pragma once

#include <cstddef>
#include <cstdlib>
#include <string>
#include <string_view>
#include <type_traits>

namespace headsetcontrol {

/**
 * @brief Convert wide string to UTF-8 string
 *
 * Handles the wide string (wchar_t*) returned by hid_error() on different platforms.
 * Uses proper wide-to-multibyte conversion to preserve non-ASCII characters.
 *
 * @param wstr Wide string pointer (can be nullptr)
 * @return UTF-8 string, or "Unknown error" if wstr is nullptr
 *
 * @note Thread-safe: Uses wcstombs which may use global locale state on some platforms.
 */
inline std::string wstring_to_string(const wchar_t* wstr)
{
    if (!wstr) {
        return "Unknown error";
    }

#ifdef _MSC_VER
    // Use wcstombs_s on MSVC (secure version)
    std::size_t required_size = 0;
    errno_t err               = wcstombs_s(&required_size, nullptr, 0, wstr, 0);
    if (err != 0 || required_size == 0) {
        // Conversion error - fall back to lossy conversion
        std::wstring ws(wstr);
        std::string result;
        result.reserve(ws.size());
        for (wchar_t wc : ws) {
            result += (wc < 128) ? static_cast<char>(wc) : '?';
        }
        return result;
    }

    std::string result(required_size - 1, '\0'); // -1 because required_size includes null terminator
    wcstombs_s(&required_size, result.data(), required_size, wstr, required_size - 1);
    return result;
#else
    // Use wcstombs on other platforms
    std::size_t required_size = std::wcstombs(nullptr, wstr, 0);
    if (required_size == static_cast<std::size_t>(-1)) {
        // Conversion error - fall back to lossy conversion
        std::wstring ws(wstr);
        std::string result;
        result.reserve(ws.size());
        for (wchar_t wc : ws) {
            result += (wc < 128) ? static_cast<char>(wc) : '?';
        }
        return result;
    }

    std::string result(required_size, '\0');
    std::wcstombs(result.data(), wstr, required_size + 1);
    return result;
#endif
}

/**
 * @brief Convert UTF-8 string to wide string
 *
 * Inverse of wstring_to_string() for the UTF-8 strings carried in DeviceMetadata.
 * The decoding is written out rather than delegated to mbstowcs() so that it does
 * not depend on the process locale: a device-supplied name arrives as raw bytes
 * off the wire and never passes through the C library's conversion on the way in.
 *
 * Widening byte by byte instead would produce one wide character per byte, so a
 * name like "Muller" spelled with an umlaut would come back out mangled.
 *
 * Malformed input is replaced with U+FFFD rather than rejected, so one bad byte
 * in a name read from a device does not discard the rest of it.
 *
 * @param str UTF-8 string
 * @return Wide string: UTF-16 where wchar_t is 16 bits, UTF-32 where it is wider
 */
inline std::wstring string_to_wstring(std::string_view str)
{
    constexpr char32_t REPLACEMENT   = 0xFFFD;
    constexpr char32_t MAX_CODEPOINT = 0x10FFFF;

    std::wstring result;
    result.reserve(str.size());

    auto append = [&result](char32_t codepoint) {
        if constexpr (sizeof(wchar_t) >= 4) {
            result += static_cast<wchar_t>(codepoint);
        } else if (codepoint <= 0xFFFF) {
            result += static_cast<wchar_t>(codepoint);
        } else {
            // Split into a UTF-16 surrogate pair, as on Windows where wchar_t is 16 bits
            const char32_t offset = codepoint - 0x10000;
            result += static_cast<wchar_t>(0xD800 + (offset >> 10));
            result += static_cast<wchar_t>(0xDC00 + (offset & 0x3FF));
        }
    };

    for (std::size_t i = 0; i < str.size();) {
        const auto lead = static_cast<unsigned char>(str[i]);

        // Length of the sequence, and the smallest codepoint it may legally encode -
        // a larger sequence than a codepoint needs is an overlong encoding
        std::size_t continuations = 0;
        char32_t codepoint        = 0;
        char32_t minimum          = 0;
        if (lead < 0x80) {
            codepoint = lead;
        } else if ((lead & 0xE0) == 0xC0) {
            continuations = 1;
            codepoint     = lead & 0x1FU;
            minimum       = 0x80;
        } else if ((lead & 0xF0) == 0xE0) {
            continuations = 2;
            codepoint     = lead & 0x0FU;
            minimum       = 0x800;
        } else if ((lead & 0xF8) == 0xF0) {
            continuations = 3;
            codepoint     = lead & 0x07U;
            minimum       = 0x10000;
        } else {
            append(REPLACEMENT);
            ++i;
            continue;
        }

        if (i + continuations >= str.size()) {
            append(REPLACEMENT);
            ++i;
            continue;
        }

        bool valid = true;
        for (std::size_t k = 1; k <= continuations; ++k) {
            const auto continuation = static_cast<unsigned char>(str[i + k]);
            if ((continuation & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            codepoint = (codepoint << 6) | (continuation & 0x3FU);
        }

        // Surrogates are not valid on their own, and are not encodable in UTF-8
        const bool is_surrogate = codepoint >= 0xD800 && codepoint <= 0xDFFF;
        if (!valid || codepoint < minimum || codepoint > MAX_CODEPOINT || is_surrogate) {
            append(REPLACEMENT);
            ++i;
            continue;
        }

        append(codepoint);
        i += continuations + 1;
    }

    return result;
}

/**
 * @brief Convert wide string to UTF-8 string
 *
 * Counterpart to string_to_wstring(), and an exact inverse of it. Unlike
 * wstring_to_string() this does not consult the locale: it is for strings that are
 * defined to be UTF-8, such as the device names in DeviceMetadata, where going
 * through wcstombs() would replace anything outside the current locale's encoding
 * with '?' - which in the default C locale means every non-ASCII character.
 *
 * Where wchar_t is 16 bits a surrogate pair is recombined into the codepoint it
 * encodes; an unpaired surrogate is replaced with U+FFFD.
 *
 * @param str Wide string
 * @return UTF-8 string
 */
inline std::string wstring_to_utf8(std::wstring_view str)
{
    constexpr char32_t REPLACEMENT   = 0xFFFD;
    constexpr char32_t MAX_CODEPOINT = 0x10FFFF;

    std::string result;
    result.reserve(str.size());

    auto append = [&result](char32_t codepoint) {
        if (codepoint < 0x80) {
            result += static_cast<char>(codepoint);
        } else if (codepoint < 0x800) {
            result += static_cast<char>(0xC0 | (codepoint >> 6));
            result += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else if (codepoint < 0x10000) {
            result += static_cast<char>(0xE0 | (codepoint >> 12));
            result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else {
            result += static_cast<char>(0xF0 | (codepoint >> 18));
            result += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
            result += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
    };

    for (std::size_t i = 0; i < str.size(); ++i) {
        // Mask rather than cast: char32_t is unsigned, but wchar_t is signed on
        // some platforms, and a plain conversion would sign-extend
        auto codepoint = static_cast<char32_t>(
            static_cast<std::make_unsigned_t<wchar_t>>(str[i]));

        if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
            // High surrogate: needs the matching low surrogate to mean anything
            const bool has_low = i + 1 < str.size();
            const auto low     = has_low
                    ? static_cast<char32_t>(static_cast<std::make_unsigned_t<wchar_t>>(str[i + 1]))
                    : char32_t { 0 };
            if (has_low && low >= 0xDC00 && low <= 0xDFFF) {
                codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            } else {
                codepoint = REPLACEMENT;
            }
        } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
            // Low surrogate without a high one before it
            codepoint = REPLACEMENT;
        }

        if (codepoint > MAX_CODEPOINT) {
            codepoint = REPLACEMENT;
        }
        append(codepoint);
    }

    return result;
}

} // namespace headsetcontrol
