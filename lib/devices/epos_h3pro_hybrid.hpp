#pragma once

#include "hid_device.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief EPOS H3PRO Hybrid wireless gaming headset
 *
 * Works through its USB dongle (VID 0x1395, PID 0x0303) or with the headset
 * plugged in by cable (PID 0x0101). Both expose the same vendor collection on
 * HID interface 3 (usage page 0xFFFF, usage 0x0001), with 106-byte input and
 * output reports (report ID 1).
 *
 * Each report carries one frame of little-endian 16-bit fields:
 *
 *   eb fe, 0x0001, type, target, payload length, CRC, payload...
 *
 *   type     0x0004 for requests and their replies, 0x0110 for notifications
 *   target   0x0001 for the plugged-in device (dongle or cabled headset),
 *            0x0005 for the headset behind the dongle. Replies come back with
 *            the target minus one (0x0000, 0x0004).
 *   CRC      CRC-8 (polynomial 0x07, initial value 0xFF) over the payload
 *   payload  16-bit words: group, command, values...
 *
 * Known commands, captured from EPOS Gaming Suite:
 *   01/09         Dongle only: 1 while the headset is connected, 0 otherwise
 *   12/03         Battery percentage (seen in steps of 20)
 *   0a/0b level   Set sidetone, 0-255; replies with status 0
 *   0a/0c         Read the sidetone level
 *   0a/11 on      Set mic noise cancellation, 0 or 1; replies with status 0
 *   0a/12         Read mic noise cancellation
 *   01/1f on      Set auto sleep, 0 or 1; replies with status 0
 *   01/20         Read auto sleep
 *
 * While the headset is off, the dongle returns requests for the headset
 * unanswered with target 0x0001.
 *
 * Auto sleep is not exposed here: Gaming Suite describes it as a low power mode
 * after a few seconds, which none of the capabilities match. The smart button
 * setting is handled by Gaming Suite alone and sends nothing to the headset.
 *
 * The headset also reports its noise cancellation slider (17/07), which
 * is independent of the mic noise cancellation; no command to set it is known.
 */
class EposH3ProHybrid : public HIDDevice {
public:
    static constexpr uint16_t PRODUCT_ID_DONGLE = 0x0303;
    static constexpr uint16_t PRODUCT_ID_CABLE  = 0x0101;
    static constexpr std::array<uint16_t, 2> PRODUCT_IDS { PRODUCT_ID_DONGLE, PRODUCT_ID_CABLE };

    static constexpr uint8_t REPORT_ID     = 0x01;
    static constexpr size_t REPORT_SIZE    = 107; // Report ID and 106 bytes
    static constexpr size_t HEADER_SIZE    = 13; // Report ID, magic and five fields
    static constexpr uint16_t VERSION      = 0x0001;
    static constexpr uint16_t TYPE_REQUEST = 0x0004;

    static constexpr uint16_t TARGET_DEVICE  = 0x0001;
    static constexpr uint16_t TARGET_HEADSET = 0x0005;

    struct Command {
        uint16_t group;
        uint16_t id;
    };

    static constexpr Command CMD_CONNECTED { 0x01, 0x09 };
    static constexpr Command CMD_BATTERY { 0x12, 0x03 };
    static constexpr Command CMD_SIDETONE_SET { 0x0A, 0x0B };
    static constexpr Command CMD_SIDETONE_GET { 0x0A, 0x0C };
    static constexpr Command CMD_NOISE_FILTER_SET { 0x0A, 0x11 };

    static constexpr uint16_t STATUS_SUCCESS     = 0;
    static constexpr uint8_t SIDETONE_DEVICE_MAX = 0xFF;

    struct Frame {
        uint16_t type;
        uint16_t target;
        uint16_t group;
        uint16_t command;
        std::vector<uint16_t> values;
    };

    uint16_t getVendorId() const override { return 0x1395; }

    std::vector<uint16_t> getProductIds() const override
    {
        return { PRODUCT_IDS.begin(), PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override { return "EPOS H3PRO Hybrid"sv; }

    constexpr int getCapabilities() const override
    {
        return B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_NOISE_FILTER);
    }

    constexpr int getReadableCapabilities() const override
    {
        return B(CAP_SIDETONE);
    }

    constexpr capability_detail getCapabilityDetail([[maybe_unused]] enum capabilities cap) const override
    {
        return { .usagepage = 0xFFFF, .usageid = 0x0001, .interface_id = 3 };
    }

    [[nodiscard]] static constexpr uint8_t crc8(std::span<const uint8_t> data)
    {
        uint8_t crc = 0xFF;
        for (uint8_t byte : data) {
            crc ^= byte;
            for (int bit = 0; bit < 8; ++bit) {
                crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x07) : static_cast<uint8_t>(crc << 1);
            }
        }
        return crc;
    }

    /**
     * @brief Map a sidetone level (0-128) to the device's level (0-255)
     */
    [[nodiscard]] static constexpr uint8_t sidetoneDeviceLevel(uint8_t level)
    {
        int capped = std::min<int>(level, 128);
        return static_cast<uint8_t>((capped * SIDETONE_DEVICE_MAX + 64) / 128);
    }

    /**
     * @brief Map the device's sidetone level (0-255) to a sidetone level (0-128)
     */
    [[nodiscard]] static constexpr uint8_t sidetoneLevel(uint16_t device_level)
    {
        int capped = std::min<int>(device_level, SIDETONE_DEVICE_MAX);
        return static_cast<uint8_t>((capped * 128 + SIDETONE_DEVICE_MAX / 2) / SIDETONE_DEVICE_MAX);
    }

    [[nodiscard]] static std::array<uint8_t, REPORT_SIZE> buildRequest(uint16_t target, Command command,
        std::optional<uint16_t> value = std::nullopt)
    {
        std::vector<uint8_t> payload;
        auto append = [&payload](uint16_t word) {
            payload.push_back(static_cast<uint8_t>(word & 0xFF));
            payload.push_back(static_cast<uint8_t>(word >> 8));
        };
        append(command.group);
        append(command.id);
        if (value) {
            append(*value);
        }

        std::array<uint8_t, REPORT_SIZE> report {};
        size_t position    = 0;
        report[position++] = REPORT_ID;
        report[position++] = 0xEB;
        report[position++] = 0xFE;
        auto put           = [&report, &position](uint16_t word) {
            report[position++] = static_cast<uint8_t>(word & 0xFF);
            report[position++] = static_cast<uint8_t>(word >> 8);
        };
        put(VERSION);
        put(TYPE_REQUEST);
        put(target);
        put(static_cast<uint16_t>(payload.size()));
        put(crc8(payload));
        std::copy(payload.begin(), payload.end(), report.begin() + position);
        return report;
    }

    /**
     * @brief Decode the frame in an input report
     *
     * @return The frame, or nullopt for other reports and frames with a bad CRC
     */
    [[nodiscard]] static std::optional<Frame> parseFrame(std::span<const uint8_t> report)
    {
        if (report.size() < HEADER_SIZE || report[0] != REPORT_ID || report[1] != 0xEB || report[2] != 0xFE) {
            return std::nullopt;
        }
        auto word = [&report](size_t offset) {
            return static_cast<uint16_t>(report[offset] | (report[offset + 1] << 8));
        };
        uint16_t length = word(9);
        if (length < 4 || length % 2 != 0 || HEADER_SIZE + length > report.size()) {
            return std::nullopt;
        }
        auto payload = report.subspan(HEADER_SIZE, length);
        if (word(11) != crc8(payload)) {
            return std::nullopt;
        }

        Frame frame { .type = word(5), .target = word(7), .group = word(13), .command = word(15), .values = {} };
        for (size_t offset = HEADER_SIZE + 4; offset < HEADER_SIZE + length; offset += 2) {
            frame.values.push_back(word(offset));
        }
        return frame;
    }

    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        if (auto connected = checkHeadsetConnected(device_handle); !connected) {
            return connected.error();
        }

        auto reply = request(device_handle, CMD_BATTERY);
        if (!reply) {
            return reply.error();
        }
        if (reply->size() != 1 || (*reply)[0] > 100) {
            return DeviceError::protocolError("Unexpected battery reply");
        }

        auto level = static_cast<uint8_t>((*reply)[0]);
        return BatteryResult {
            .level_percent = level,
            .status        = BATTERY_AVAILABLE,
            .raw_data      = std::vector<uint8_t> { level },
        };
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        if (auto connected = checkHeadsetConnected(device_handle); !connected) {
            return connected.error();
        }

        uint8_t device_level = sidetoneDeviceLevel(level);
        if (auto set = setValue(device_handle, CMD_SIDETONE_SET, device_level); !set) {
            return set.error();
        }

        return sidetoneResult(device_level);
    }

    Result<SidetoneResult> getSidetone(hid_device* device_handle) override
    {
        if (auto connected = checkHeadsetConnected(device_handle); !connected) {
            return connected.error();
        }

        auto reply = request(device_handle, CMD_SIDETONE_GET);
        if (!reply) {
            return reply.error();
        }
        if (reply->size() != 1 || (*reply)[0] > SIDETONE_DEVICE_MAX) {
            return DeviceError::protocolError("Unexpected sidetone reply");
        }

        return sidetoneResult(static_cast<uint8_t>((*reply)[0]));
    }

    Result<NoiseFilterResult> setNoiseFilter(hid_device* device_handle, uint8_t level) override
    {
        // The headset only switches its mic noise cancellation on or off
        if (level > 1) {
            return DeviceError::invalidParameter("Noise filter level must be 0 (off) or 1 (on)");
        }
        if (auto connected = checkHeadsetConnected(device_handle); !connected) {
            return connected.error();
        }

        if (auto set = setValue(device_handle, CMD_NOISE_FILTER_SET, level); !set) {
            return set.error();
        }

        return NoiseFilterResult { .level = level };
    }

private:
    [[nodiscard]] bool isDongle() const { return getMatchedProductId() != PRODUCT_ID_CABLE; }

    [[nodiscard]] uint16_t headsetTarget() const { return isDongle() ? TARGET_HEADSET : TARGET_DEVICE; }

    static SidetoneResult sidetoneResult(uint8_t device_level)
    {
        return SidetoneResult {
            .current_level = sidetoneLevel(device_level),
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = 0,
            .device_max    = SIDETONE_DEVICE_MAX,
            .device_level  = device_level,
        };
    }

    /**
     * @brief Ask the dongle whether the headset is connected
     *
     * Saves waiting for a reply the dongle won't forward. Always succeeds for a
     * headset plugged in by cable.
     */
    Result<void> checkHeadsetConnected(hid_device* device_handle) const
    {
        if (!isDongle()) {
            return {};
        }

        auto reply = send(device_handle, TARGET_DEVICE, CMD_CONNECTED);
        if (!reply) {
            return reply.error();
        }
        if (reply->size() != 1) {
            return DeviceError::protocolError("Unexpected connection reply");
        }
        if ((*reply)[0] == 0) {
            return DeviceError::deviceOffline("Headset is switched off or out of range");
        }
        return {};
    }

    Result<void> setValue(hid_device* device_handle, Command command, uint16_t value) const
    {
        auto reply = request(device_handle, command, value);
        if (!reply) {
            return reply.error();
        }
        if (reply->size() != 1 || (*reply)[0] != STATUS_SUCCESS) {
            return DeviceError::protocolError("Command failed");
        }
        return {};
    }

    Result<std::vector<uint16_t>> request(hid_device* device_handle, Command command,
        std::optional<uint16_t> value = std::nullopt) const
    {
        return send(device_handle, headsetTarget(), command, value);
    }

    /**
     * @brief Send a request and wait for its reply
     *
     * Skips notifications and other input reports, such as media keys.
     *
     * @return The values of the reply
     */
    Result<std::vector<uint16_t>> send(hid_device* device_handle, uint16_t target, Command command,
        std::optional<uint16_t> value = std::nullopt) const
    {
        if (auto written = writeHID(device_handle, buildRequest(target, command, value)); !written) {
            return written.error();
        }

        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(hsc_device_timeout);
        while (true) {
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
            if (remaining.count() <= 0) {
                return DeviceError::timeout("No reply from the H3PRO");
            }

            std::array<uint8_t, REPORT_SIZE> buffer {};
            auto count = readHIDTimeout(device_handle, buffer, static_cast<int>(remaining.count()));
            if (!count) {
                if (count.error().code == DeviceError::Code::Timeout) {
                    return DeviceError::timeout("No reply from the H3PRO");
                }
                return count.error();
            }

            auto frame = parseFrame(std::span<const uint8_t>(buffer.data(), *count));
            if (!frame || frame->group != command.group || frame->command != command.id) {
                continue;
            }
            if (target == TARGET_HEADSET && frame->target == TARGET_DEVICE) {
                return DeviceError::deviceOffline("Headset is switched off or out of range");
            }
            if (frame->target == target - 1) {
                return std::move(frame->values);
            }
        }
    }
};

} // namespace headsetcontrol
