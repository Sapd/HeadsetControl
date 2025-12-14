#pragma once

#include <cstdlib>
#include <string>

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

    // Calculate required buffer size
    std::size_t required_size = std::wcstombs(nullptr, wstr, 0);
    if (required_size == static_cast<std::size_t>(-1)) {
        // Conversion error - fall back to lossy conversion
        std::wstring ws(wstr);
        std::string result;
        result.reserve(ws.size());
        for (wchar_t wc : ws) {
            // Only keep ASCII characters, replace others with '?'
            result += (wc < 128) ? static_cast<char>(wc) : '?';
        }
        return result;
    }

    // Perform actual conversion
    std::string result(required_size, '\0');
    std::wcstombs(result.data(), wstr, required_size + 1);
    return result;
}

} // namespace headsetcontrol
