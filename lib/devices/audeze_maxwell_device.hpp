#pragma once

#include "../result_types.hpp"
#include "device_utils.hpp"
#include "hid_device.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <optional>
#include <span>
#include <thread>
#include <vector>

namespace headsetcontrol {

/**
 * @brief Base class for Audeze Maxwell devices (Maxwell and Maxwell 2)
 *
 * Implements the Airoha based control protocol both models share.
 *
 * Recovered from Audeze.exe and a USB capture of the original Maxwell dongle.
 *
 * HID reports are 62 bytes: output report 0x06 via hid_write, input report 0x07 via
 * hid_get_input_report.
 *
 *   <report id> <len> <route> <fragment...>
 *
 *   len   : number of valid fragment bytes, 0..59. Everything after them is padding or
 *           leftovers of older reports and must be ignored. A report with len 0 means
 *           "nothing new yet", whatever its remaining bytes look like.
 *   route : 0x80 to reach the headset through the dongle
 *
 * The fragments form a stream of messages. One report can carry several messages and a
 * message can span several reports:
 *
 *   05 <type> <len lo> <len hi> <body...>
 *
 *   type : 5A request, 5B response, 5C notification, 5D asynchronous result
 *   len  : body length; the first two body bytes identify the command
 *
 * Commands used, as request body -> reply body. Fixed bytes are written in hex, the
 * bytes that vary are named:
 *
 *   <key>    : which setting, in the 09 family (the KEY_ constants)
 *   <param>  : which setting, in the 2C family (the PARAM_ constants)
 *   <value>  : the setting itself; one byte unless noted
 *   <status> : 00 on success
 *
 *   read key          01 09 <key> 00            -> 5B: 01 09 <key> 00 <status> <value>
 *   write key         00 09 <key> 00 <value>    -> 5B: 00 09 <key> 00 00
 *   read parameter    83 2C <param> 00          -> 5B: 83 2C <status> <param> 00 <value>
 *   write parameter   82 2C <param> 00 <value>  -> 5B: 82 2C <status> <param> 00
 *   battery           D6 0C 00                  -> 5B: D6 0C <status>
 *                                                  5D: D6 0C 00 00 <percent>
 *
 * A parameter value can be several bytes long (auto power off). Writing the sidetone
 * parameter toggles sidetone whatever value is sent.
 *
 * Replies carry no sequence number, so they are matched on the command identity and
 * anything else that is queued is skipped.
 */
class AudezeMaxwellDevice : public HIDDevice {
public:
    static constexpr size_t MSG_SIZE          = 62;
    static constexpr uint8_t REPORT_ID_OUT    = 0x06;
    static constexpr uint8_t REPORT_ID_IN     = 0x07;
    static constexpr size_t FRAGMENT_OFFSET   = 3;
    static constexpr size_t MAX_FRAGMENT_SIZE = MSG_SIZE - FRAGMENT_OFFSET;
    static constexpr uint8_t ROUTE_HEADSET    = 0x80;

    static constexpr uint8_t MESSAGE_MARKER  = 0x05;
    static constexpr uint8_t TYPE_REQUEST    = 0x5A;
    static constexpr uint8_t TYPE_RESPONSE   = 0x5B;
    static constexpr uint8_t TYPE_RESULT     = 0x5D;
    static constexpr size_t TYPE_OFFSET      = 1;
    static constexpr size_t LENGTH_OFFSET    = 2;
    static constexpr size_t HEADER_SIZE      = 4;
    static constexpr size_t MAX_MESSAGE_SIZE = 1024;

    static constexpr uint8_t STATUS_OK = 0x00;

    using Message = std::vector<uint8_t>;
    using Report  = std::array<uint8_t, MSG_SIZE>;

    /// The first two body bytes, which identify a command
    using Command = std::array<uint8_t, 2>;

    static constexpr Command CMD_WRITE_KEY { 0x00, 0x09 };
    static constexpr Command CMD_READ_KEY { 0x01, 0x09 };
    static constexpr Command CMD_WRITE_PARAMETER { 0x82, 0x2C };
    static constexpr Command CMD_READ_PARAMETER { 0x83, 0x2C };
    static constexpr Command CMD_BATTERY { 0xD6, 0x0C };

    // Keys of the 01 09 (read) / 00 09 (write) family
    static constexpr uint8_t KEY_EQ_PRESET          = 0x00;
    static constexpr uint8_t KEY_VOICE_PROMPT_LEVEL = 0x20;
    static constexpr uint8_t KEY_MIC_MUTE           = 0x22;
    static constexpr uint8_t KEY_NOISE_FILTER       = 0x24;
    static constexpr uint8_t KEY_VOLUME_LIMITER     = 0x28;
    static constexpr uint8_t KEY_SIDETONE_LEVEL     = 0x2C;
    static constexpr uint8_t KEY_SIDETONE_MAX_LEVEL = 0x2D;

    // Parameters of the 83 2C (read) / 82 2C (write) family
    static constexpr uint8_t PARAM_AUTO_POWER_OFF   = 0x01;
    static constexpr uint8_t PARAM_SIDETONE_ENABLED = 0x07;
    static constexpr uint8_t PARAM_CHATMIX          = 0x0B;

    // KEY_MIC_MUTE reads 00 while muted and FF otherwise
    static constexpr uint8_t MIC_MUTED = 0x00;

    // Voice prompt level: 0 (0%) to 15 (100%), default 7 (50%)
    static constexpr uint8_t VOICE_PROMPT_LEVEL_OFF     = 0x00;
    static constexpr uint8_t VOICE_PROMPT_LEVEL_DEFAULT = 0x07;

    static constexpr uint8_t SIDETONE_API_MAX     = 128;
    static constexpr uint8_t DEFAULT_SIDETONE_MAX = 31;
    static constexpr uint8_t CHATMIX_MAX          = 20;
    static constexpr uint8_t NOISE_FILTER_MAX     = 2;

    static constexpr int EQUALIZER_PRESETS_COUNT = 10;

    constexpr uint16_t getVendorId() const override
    {
        return VENDOR_AUDEZE;
    }

    uint8_t getEqualizerPresetsCount() const override
    {
        return EQUALIZER_PRESETS_COUNT;
    }

    /// Append a 16-bit value, low byte first
    static void appendLE16(Message& bytes, uint16_t value)
    {
        bytes.push_back(static_cast<uint8_t>(value & 0xFF));
        bytes.push_back(static_cast<uint8_t>(value >> 8));
    }

    /// The 16-bit value stored low byte first at @p offset
    static uint16_t readLE16(std::span<const uint8_t> bytes, size_t offset)
    {
        return static_cast<uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
    }

    /// Build a message: 05 <type> <len lo> <len hi> <body>
    static Message buildMessage(uint8_t type, std::span<const uint8_t> body)
    {
        Message message { MESSAGE_MARKER, type };
        appendLE16(message, static_cast<uint16_t>(body.size()));
        message.insert(message.end(), body.begin(), body.end());
        return message;
    }

    /// Body of a request: the command followed by its arguments
    static Message commandBody(Command command, std::initializer_list<uint8_t> arguments = {})
    {
        Message body(command.begin(), command.end());
        body.insert(body.end(), arguments.begin(), arguments.end());
        return body;
    }

    /// Split a message into zero padded output reports
    static std::vector<Report> buildReports(std::span<const uint8_t> message)
    {
        std::vector<Report> reports;
        for (size_t offset = 0; offset < message.size(); offset += MAX_FRAGMENT_SIZE) {
            const size_t length = std::min(MAX_FRAGMENT_SIZE, message.size() - offset);

            Report report {};
            report[0] = REPORT_ID_OUT;
            report[1] = static_cast<uint8_t>(length);
            report[2] = ROUTE_HEADSET;
            std::copy_n(message.begin() + offset, length, report.begin() + FRAGMENT_OFFSET);
            reports.push_back(report);
        }
        return reports;
    }

    /// The valid fragment of an input report; empty if it carries nothing new
    static std::span<const uint8_t> reportFragment(std::span<const uint8_t> report)
    {
        if (report.size() < FRAGMENT_OFFSET) {
            return {};
        }
        const size_t length = report[1];
        if (length > MAX_FRAGMENT_SIZE || FRAGMENT_OFFSET + length > report.size()) {
            return {};
        }
        return report.subspan(FRAGMENT_OFFSET, length);
    }

    /**
     * @brief Take the next complete message off the front of a fragment stream
     *
     * Bytes that cannot start a message are dropped. Returns an empty message if the
     * stream holds no complete message yet.
     */
    static Message extractMessage(std::vector<uint8_t>& stream)
    {
        while (!stream.empty()) {
            if (stream[0] != MESSAGE_MARKER) {
                stream.erase(stream.begin());
                continue;
            }
            if (stream.size() < HEADER_SIZE) {
                break;
            }
            const size_t size = HEADER_SIZE + readLE16(stream, LENGTH_OFFSET);
            if (size > MAX_MESSAGE_SIZE) {
                stream.erase(stream.begin());
                continue;
            }
            if (stream.size() < size) {
                break;
            }
            Message message(stream.begin(), stream.begin() + size);
            stream.erase(stream.begin(), stream.begin() + size);
            return message;
        }
        return {};
    }

protected:
    // Non-static so tests can shorten them
    std::chrono::milliseconds response_timeout_ { 400 };
    std::chrono::milliseconds poll_interval_ { 20 };

    static constexpr int MAX_ATTEMPTS = 3;

    /// How a request is sent and how long its reply is awaited
    struct RequestOptions {
        /// How often the request is sent while it times out; 1 if it is not safe to repeat
        int attempts = MAX_ATTEMPTS;
        /// How long to wait for the reply; response_timeout_ if not set
        std::optional<std::chrono::milliseconds> timeout {};
        /// Pause between the reports of a request that does not fit into one
        std::chrono::milliseconds fragment_interval { 0 };
    };

    /// True if a message is of the given type and belongs to the given command
    static bool isMessage(const Message& message, uint8_t type, Command command)
    {
        return message.size() >= HEADER_SIZE + command.size()
            && message[TYPE_OFFSET] == type
            && std::equal(command.begin(), command.end(), message.begin() + HEADER_SIZE);
    }

    /// True if a message carries the 16-bit @p id (a key or a parameter) at @p offset
    static bool hasId(const Message& message, size_t offset, uint16_t id)
    {
        return message.size() >= offset + 2 && readLE16(message, offset) == id;
    }

    /**
     * @brief Wait for the message accepted by @p accept, skipping everything else
     *
     * The headset answers after roughly 60-120 ms, and until then the dongle returns
     * reports with length 0, so the input report is polled until the timeout.
     */
    template <typename Predicate>
    Result<Message> awaitMessage(hid_device* device_handle, Predicate accept, std::chrono::milliseconds timeout) const
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        std::vector<uint8_t> stream;

        while (true) {
            for (auto message = extractMessage(stream); !message.empty(); message = extractMessage(stream)) {
                if (accept(message)) {
                    return message;
                }
            }

            if (std::chrono::steady_clock::now() >= deadline) {
                return DeviceError::timeout("No matching reply from the Maxwell");
            }
            std::this_thread::sleep_for(poll_interval_);

            Report report { REPORT_ID_IN };
            auto read_result = getInputReport(device_handle, report);
            if (!read_result) {
                return read_result.error();
            }

            const auto fragment = reportFragment(std::span<const uint8_t>(report).first(std::min(*read_result, report.size())));
            stream.insert(stream.end(), fragment.begin(), fragment.end());
        }
    }

    /// Write the reports of one request, pausing for @p interval between them
    Result<void> sendReports(hid_device* device_handle, std::span<const Report> reports, std::chrono::milliseconds interval) const
    {
        for (size_t i = 0; i < reports.size(); ++i) {
            if (i > 0 && interval.count() > 0) {
                std::this_thread::sleep_for(interval);
            }
            if (auto result = writeHID(device_handle, reports[i], MSG_SIZE); !result) {
                return result.error();
            }
        }
        return {};
    }

    /**
     * @brief Send a request and return the reply accepted by @p accept
     *
     * Reading an input report consumes it, so another program polling the same dongle
     * (the Audeze app does so about once a second) can take a reply meant for us, while
     * we see its replies instead. The request is therefore repeated on a timeout, unless
     * @p options says otherwise.
     */
    template <typename Predicate>
    Result<Message> request(hid_device* device_handle, const Message& body, Predicate accept, const RequestOptions& options) const
    {
        const auto reports = buildReports(buildMessage(TYPE_REQUEST, body));
        const auto timeout = options.timeout.value_or(response_timeout_);

        Result<Message> reply = DeviceError::timeout("No matching reply from the Maxwell");
        for (int attempt = 0; attempt < options.attempts; ++attempt) {
            if (auto result = sendReports(device_handle, reports, options.fragment_interval); !result) {
                return result.error();
            }

            reply = awaitMessage(device_handle, accept, timeout);
            if (reply || reply.error().code != DeviceError::Code::Timeout) {
                break;
            }
        }
        return reply;
    }

    template <typename Predicate>
    Result<Message> request(hid_device* device_handle, const Message& body, Predicate accept) const
    {
        return request(device_handle, body, accept, RequestOptions {});
    }

    Result<uint8_t> readKey(hid_device* device_handle, uint8_t key) const
    {
        // Reply: 05 5B 06 00 01 09 <key> 00 <status> <value>
        static constexpr size_t KEY_OFFSET    = 6;
        static constexpr size_t STATUS_OFFSET = 8;
        static constexpr size_t VALUE_OFFSET  = 9;

        auto reply = request(device_handle, commandBody(CMD_READ_KEY, { key, 0x00 }), [key](const Message& message) {
            return isMessage(message, TYPE_RESPONSE, CMD_READ_KEY) && hasId(message, KEY_OFFSET, key);
        });
        if (!reply) {
            return reply.error();
        }
        if (reply->size() <= VALUE_OFFSET || (*reply)[STATUS_OFFSET] != STATUS_OK) {
            return DeviceError::protocolError(std::format("Maxwell rejected reading key 0x{:02x}", key));
        }
        return (*reply)[VALUE_OFFSET];
    }

    Result<void> writeKey(hid_device* device_handle, uint8_t key, uint8_t value) const
    {
        // Reply: 05 5B 05 00 00 09 <key> 00 00
        static constexpr size_t FAMILY_OFFSET = 5;
        static constexpr size_t KEY_OFFSET    = 6;

        // Audeze.exe only checks the family byte and the key of this acknowledgement
        auto reply = request(device_handle, commandBody(CMD_WRITE_KEY, { key, 0x00, value }), [key](const Message& message) {
            return message.size() > KEY_OFFSET && message[TYPE_OFFSET] == TYPE_RESPONSE
                && message[FAMILY_OFFSET] == CMD_WRITE_KEY[1] && message[KEY_OFFSET] == key;
        });
        if (!reply) {
            return reply.error();
        }
        return {};
    }

    Result<uint8_t> readParameter(hid_device* device_handle, uint8_t parameter) const
    {
        // Reply: 05 5B 06 00 83 2C <status> <param> 00 <value>
        static constexpr size_t STATUS_OFFSET    = 6;
        static constexpr size_t PARAMETER_OFFSET = 7;
        static constexpr size_t VALUE_OFFSET     = 9;

        auto reply = request(device_handle, commandBody(CMD_READ_PARAMETER, { parameter, 0x00 }), [parameter](const Message& message) {
            return isMessage(message, TYPE_RESPONSE, CMD_READ_PARAMETER) && hasId(message, PARAMETER_OFFSET, parameter);
        });
        if (!reply) {
            return reply.error();
        }
        if (reply->size() <= VALUE_OFFSET || (*reply)[STATUS_OFFSET] != STATUS_OK) {
            return DeviceError::protocolError(std::format("Maxwell rejected reading parameter 0x{:02x}", parameter));
        }
        return (*reply)[VALUE_OFFSET];
    }

    Result<void> writeParameter(hid_device* device_handle, uint8_t parameter, std::span<const uint8_t> value, const RequestOptions& options) const
    {
        // Reply: 05 5B 05 00 82 2C <status> <param> 00
        static constexpr size_t STATUS_OFFSET    = 6;
        static constexpr size_t PARAMETER_OFFSET = 7;

        auto body = commandBody(CMD_WRITE_PARAMETER, { parameter, 0x00 });
        body.insert(body.end(), value.begin(), value.end());

        auto reply = request(
            device_handle, body, [parameter](const Message& message) {
                return isMessage(message, TYPE_RESPONSE, CMD_WRITE_PARAMETER) && hasId(message, PARAMETER_OFFSET, parameter);
            },
            options);
        if (!reply) {
            return reply.error();
        }
        if ((*reply)[STATUS_OFFSET] != STATUS_OK) {
            return DeviceError::protocolError(std::format("Maxwell rejected writing parameter 0x{:02x}", parameter));
        }
        return {};
    }

    Result<void> writeParameter(hid_device* device_handle, uint8_t parameter, std::span<const uint8_t> value) const
    {
        return writeParameter(device_handle, parameter, value, RequestOptions {});
    }

    /// Highest sidetone level the headset accepts
    uint8_t readSidetoneMax(hid_device* device_handle) const
    {
        auto max_level = readKey(device_handle, KEY_SIDETONE_MAX_LEVEL);
        if (!max_level || *max_level == 0) {
            return DEFAULT_SIDETONE_MAX;
        }
        return *max_level;
    }

    /**
     * @brief Switch sidetone on or off
     *
     * The headset toggles sidetone on every write to this parameter, whatever value is
     * sent (verified on an original Maxwell). So the state is read first, the write is
     * only sent if it differs, and the result is read back.
     */
    Result<void> setSidetoneEnabled(hid_device* device_handle, bool enabled) const
    {
        static constexpr int MAX_TOGGLES = 2;

        for (int toggles = 0;; ++toggles) {
            auto current = readParameter(device_handle, PARAM_SIDETONE_ENABLED);
            if (!current) {
                return current.error();
            }
            const bool is_enabled = *current != 0x00;
            if (is_enabled == enabled) {
                return {};
            }
            if (toggles == MAX_TOGGLES) {
                return DeviceError::protocolError("Maxwell did not change the sidetone state");
            }

            // Sent once, as repeating it would toggle back. A lost acknowledgement does
            // not matter: the state read above decides.
            const std::array<uint8_t, 1> value { static_cast<uint8_t>(enabled ? 0x01 : 0x00) };
            auto result = writeParameter(device_handle, PARAM_SIDETONE_ENABLED, value, { .attempts = 1 });
            if (!result && result.error().code != DeviceError::Code::Timeout) {
                return result.error();
            }
        }
    }

public:
    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        // Replies: 05 5B 03 00 D6 0C <status>, then 05 5D 05 00 D6 0C 00 00 <percent>
        static constexpr size_t STATUS_OFFSET  = 6;
        static constexpr size_t PERCENT_OFFSET = 8;

        BatteryResult battery { .level_percent = -1, .status = BATTERY_UNAVAILABLE };

        // Only the result is needed; a failed acknowledgement ends the wait early
        auto result = request(device_handle, commandBody(CMD_BATTERY, { 0x00 }), [](const Message& message) {
            const bool has_level = isMessage(message, TYPE_RESULT, CMD_BATTERY) && message.size() > PERCENT_OFFSET;
            const bool refused   = isMessage(message, TYPE_RESPONSE, CMD_BATTERY) && message.size() > STATUS_OFFSET
                && message[STATUS_OFFSET] != STATUS_OK;
            return has_level || refused;
        });
        if (!result) {
            // The headset does not answer while it is switched off
            if (result.error().code == DeviceError::Code::Timeout) {
                return battery;
            }
            return result.error();
        }
        if ((*result)[TYPE_OFFSET] != TYPE_RESULT) {
            return battery;
        }

        battery.level_percent = std::min<int>((*result)[PERCENT_OFFSET], 100);
        battery.status        = BATTERY_AVAILABLE;

        if (auto mic = readKey(device_handle, KEY_MIC_MUTE); mic && *mic == MIC_MUTED) {
            battery.mic_status = MICROPHONE_UP;
        }

        return battery;
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        const uint8_t device_max = readSidetoneMax(device_handle);
        const uint8_t mapped     = map<uint8_t>(level, 0, SIDETONE_API_MAX, 0, device_max);
        const bool enabled       = mapped > 0;

        // The level and the on/off state are stored separately. Writing a level also
        // switches sidetone on, so level 0 only switches it off and keeps the stored level.
        if (enabled) {
            if (auto result = writeKey(device_handle, KEY_SIDETONE_LEVEL, mapped); !result) {
                return result.error();
            }
        }

        if (auto result = setSidetoneEnabled(device_handle, enabled); !result) {
            return result.error();
        }

        return SidetoneResult {
            .current_level = level,
            .min_level     = 0,
            .max_level     = SIDETONE_API_MAX,
            .device_min    = 0,
            .device_max    = device_max,
            .is_muted      = !enabled,
            .device_level  = mapped
        };
    }

    Result<SidetoneResult> getSidetone(hid_device* device_handle) override
    {
        auto device_level = readKey(device_handle, KEY_SIDETONE_LEVEL);
        if (!device_level) {
            return device_level.error();
        }

        auto enabled = readParameter(device_handle, PARAM_SIDETONE_ENABLED);
        if (!enabled) {
            return enabled.error();
        }

        const uint8_t device_max = std::max(readSidetoneMax(device_handle), *device_level);
        const bool muted         = *enabled == 0 || *device_level == 0;

        return SidetoneResult {
            .current_level = muted ? uint8_t(0) : map<uint8_t>(*device_level, 0, device_max, 0, SIDETONE_API_MAX),
            .min_level     = 0,
            .max_level     = SIDETONE_API_MAX,
            .device_min    = 0,
            .device_max    = device_max,
            .is_muted      = muted,
            .device_level  = *device_level
        };
    }

    Result<InactiveTimeResult> setInactiveTime(hid_device* device_handle, uint8_t minutes) override
    {
        // Round up to the nearest value the headset supports. The longest one is 6 hours,
        // which the 8-bit argument cannot express, so anything above 4 hours selects it.
        static constexpr std::array<uint16_t, 9> SUPPORTED_MINUTES { 5, 10, 15, 30, 45, 60, 90, 120, 240 };
        static constexpr uint16_t LONGEST_MINUTES = 360;

        uint16_t seconds = 0;
        if (minutes > 0) {
            const auto supported = std::ranges::lower_bound(SUPPORTED_MINUTES, minutes);
            seconds              = (supported != SUPPORTED_MINUTES.end() ? *supported : LONGEST_MINUTES) * 60;
        }

        // Enable flag and seconds (little endian), sent twice
        const uint8_t enabled = seconds > 0 ? 0x01 : 0x00;
        const auto lsb        = static_cast<uint8_t>(seconds & 0xFF);
        const auto msb        = static_cast<uint8_t>(seconds >> 8);
        const std::array<uint8_t, 8> value { enabled, 0x00, lsb, msb, enabled, 0x00, lsb, msb };
        if (auto result = writeParameter(device_handle, PARAM_AUTO_POWER_OFF, value); !result) {
            return result.error();
        }

        return InactiveTimeResult {
            .minutes     = minutes,
            .min_minutes = 0,
            .max_minutes = 255
        };
    }

    Result<VoicePromptsResult> setVoicePrompts(hid_device* device_handle, bool enabled) override
    {
        // The API has no level, so prompts are either off or at the default level
        const uint8_t prompt_level = enabled ? VOICE_PROMPT_LEVEL_DEFAULT : VOICE_PROMPT_LEVEL_OFF;
        if (auto result = writeKey(device_handle, KEY_VOICE_PROMPT_LEVEL, prompt_level); !result) {
            return result.error();
        }

        return VoicePromptsResult { .enabled = enabled };
    }

    Result<ChatmixResult> getChatmix(hid_device* device_handle) override
    {
        auto raw = readParameter(device_handle, PARAM_CHATMIX);
        if (!raw) {
            return raw.error();
        }

        // 0-20 range, 10 = center
        const int level = map(static_cast<int>(std::min(*raw, CHATMIX_MAX)), 0, static_cast<int>(CHATMIX_MAX), 0, 128);

        const int game_pct = (level >= 64) ? 100 : map(level, 0, 64, 0, 100);
        const int chat_pct = (level <= 64) ? 100 : map(level, 64, 128, 100, 0);

        return ChatmixResult {
            .level               = level,
            .game_volume_percent = game_pct,
            .chat_volume_percent = chat_pct
        };
    }

    Result<EqualizerPresetResult> setEqualizerPreset(hid_device* device_handle, uint8_t preset) override
    {
        // Maxwell supports presets 0-9 (mapped to device presets 1-10)
        // 0-5 are built-in presets, 6-9 are custom presets
        // 1 = Audeze, 2 = Treble Boost, 3 = Bass Boost, 4 = Immersive, 5 = Competition,
        // 6 = Footsteps, 7 = EQ1, 8 = EQ2, 9 = EQ3, 10 = EQ4
        if (preset >= EQUALIZER_PRESETS_COUNT) {
            return DeviceError::invalidParameter("Device only supports presets 0-9");
        }

        if (auto result = writeKey(device_handle, KEY_EQ_PRESET, static_cast<uint8_t>(preset + 1)); !result) {
            return result.error();
        }

        return EqualizerPresetResult { .preset = preset, .total_presets = EQUALIZER_PRESETS_COUNT };
    }

    Result<NoiseFilterResult> setNoiseFilter(hid_device* device_handle, uint8_t level) override
    {
        // Three levels for the noise filter: high, low, and off (2, 1 and 0)
        if (level > NOISE_FILTER_MAX) {
            return DeviceError::invalidParameter("Noise filter level must be 0, 1, or 2");
        }

        if (auto result = writeKey(device_handle, KEY_NOISE_FILTER, level); !result) {
            return result.error();
        }

        return NoiseFilterResult { .level = level };
    }
};

} // namespace headsetcontrol
