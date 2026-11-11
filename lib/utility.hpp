#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

// Forward declarations
struct ParametricEqualizerSettings;
struct LightColorSettings;

namespace headsetcontrol {

/**
 * @brief Rounds a given positive number to the nearest given multiple
 *
 * I.e. A number of 17 would be rounded to 15 if multiple is 5
 *
 * @param number A number to round
 * @param multiple A multiple
 * @return unsigned int the result rounded number
 */
unsigned int round_to_multiples(unsigned int number, unsigned int multiple);

/**
 * @brief This function calculates the estimate battery level in percent using splines.
 *
 * @param percentages percentage values to be associated with voltage values
 * @param voltages voltage values associated with percentage values
 * @param voltage readings
 * @return battery level in percent
 */
int spline_battery_level(std::span<const int> percentages, std::span<const int> voltages, uint16_t voltage);

/**
 * @brief This function calculates the estimate battery level in percent.
 *
 * To find the terms representing the polynomial discharge curve of the
 * battery a solver like https://arachnoid.com/polysolve/ can be used.
 *
 * @param terms polynomial terms for the battery discharge curve
 * @param voltage readings
 * @return battery level in percent
 */
float poly_battery_level(std::span<const double> terms, uint16_t voltage);

/**
 * @brief Helper function used during debugging for printing out binary data
 *
 * @param data data to be represented as hex string
 * @return hex string representation
 */
std::string hexdump(std::span<const uint8_t> data);

/**
 * @brief Accepts textual input and converts them to a byte buffer
 *
 * Parses data like "0xff, 123, 0xb" and converts them to a vector of bytes
 *
 * @param input string to parse
 * @return vector of parsed bytes
 */
std::vector<uint8_t> parse_byte_data(std::string_view input);

/**
 * @brief Accepts textual input and converts them to float values
 *
 * Parses data like "1.0, 2.5, 3.0" and converts them to a vector of floats
 *
 * @param input string to parse
 * @return vector of parsed floats
 */
std::vector<float> parse_float_data(std::string_view input);

/**
 * @brief Parse parametric equalizer settings string
 *
 * @param input input string or empty for reset
 * @return ParametricEqualizerSettings object
 */
ParametricEqualizerSettings parse_parametric_equalizer_settings(std::string_view input);

/**
 * @brief Parse two IDs from a string like "123:456" or "0x1b1c:0x1b27"
 *
 * A token carrying an explicit 0x prefix is always read as hexadecimal. Bare
 * tokens are read in @p default_base, so callers parsing USB vendor and product
 * IDs should pass 16: those are conventionally written unprefixed in hex, which
 * is how lsusb and this tool's own device listing print them.
 *
 * A token that is only partly numeric is rejected rather than truncated - read
 * as decimal, "1b1c" would otherwise silently become 1.
 *
 * @param input string to parse
 * @param default_base base for tokens without a 0x prefix
 * @return pair of IDs if successful, nullopt otherwise
 */
std::optional<std::pair<int, int>> parse_two_ids(std::string_view input, int default_base = 10);

/**
 * @brief Parse a color written as "RRGGBB" or "#RRGGBB"
 *
 * Exactly six hex digits, in either case. "000000" is a valid color.
 *
 * @param input string to parse
 * @return the color if successful, nullopt otherwise
 */
std::optional<LightColorSettings> parse_light_color(std::string_view input);

/**
 * @brief Lock shared between processes on this machine, held for its lifetime
 *
 * For devices whose protocol is a conversation that another process can
 * interleave with, such as a mode switch around a request: a tray app and a
 * one-off CLI call on the same dongle would otherwise read each other's
 * replies. The OS releases the lock if the holder dies.
 *
 * - Windows: a named mutex, "Global\HeadsetControl-<name>" so that sessions
 *   (a console login and an SSH session) share it; "Local\" if the global
 *   one cannot be opened, e.g. because another user created it.
 * - Elsewhere: flock() on /tmp/headsetcontrol-<name>.lock. The path does not
 *   depend on the environment, so sudo, cron and a desktop session agree.
 *   The file is opened without following symlinks and must be a plain file.
 *
 * Do not take the same name twice in one process: a Windows mutex is
 * recursive per thread, flock() is not.
 *
 * If the OS facility fails, status() is Unavailable and the caller should
 * carry on without the lock: losing the lock is better than losing the
 * device.
 */
class ProcessLock {
public:
    enum class Status {
        Held, ///< ours until destruction
        Busy, ///< another holder kept it past the timeout
        Unavailable, ///< the OS facility failed; nothing is locked
    };

    /// @param name short identifier; characters outside [A-Za-z0-9._-] become '_'
    ProcessLock(std::string_view name, std::chrono::milliseconds timeout);
    ~ProcessLock();
    ProcessLock(const ProcessLock&)            = delete;
    ProcessLock& operator=(const ProcessLock&) = delete;
    ProcessLock(ProcessLock&&)                 = delete;
    ProcessLock& operator=(ProcessLock&&)      = delete;

    [[nodiscard]] Status status() const { return status_; }

    /// Held, and the previous holder died while holding it (Windows only)
    [[nodiscard]] bool recovered() const { return recovered_; }

private:
    Status status_  = Status::Unavailable;
    bool recovered_ = false;
#ifdef _WIN32
    void* handle_ = nullptr;
#else
    int fd_ = -1;
#endif
};

/**
 * @brief Cross-platform sleep for milliseconds
 */
inline void sleep_ms(unsigned int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

} // namespace headsetcontrol

// utilities
#include "devices/device_utils.hpp"
#include "string_utils.hpp"

// Bring commonly used functions into global namespace for convenience
using headsetcontrol::map;
using headsetcontrol::round_to_multiples;
using headsetcontrol::sleep_ms;
