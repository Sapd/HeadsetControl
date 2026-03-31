#pragma once

#include "../utility.hpp"
#include "device.hpp"
#include "hid_device.hpp"
#include "result_types.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Logitech G522 LIGHTSPEED (PID 0x0b18)
 *
 * This variant uses a vendor-specific 64-byte protocol on usage page 0xffa0
 * for battery data instead of HID++.
 *
 * This implementation is modified from the working LIGHTSPEED implementation for the G PRO X 2 (PID 0x0af7) as the used protocol appears to be the same.
 */
class LogitechG522Lightspeed : public HIDDevice {
public:
    static constexpr std::array<uint16_t, 1> SUPPORTED_PRODUCT_IDS { 0x0b18 };
    static constexpr size_t PACKET_SIZE = 64;
    static constexpr std::array<uint8_t, 2> REPORT_PREFIX { 0x50, 0x23 };

    constexpr uint16_t getVendorId() const override
    {
        return VENDOR_LOGITECH;
    }

    std::vector<uint16_t> getProductIds() const override
    {
        return { SUPPORTED_PRODUCT_IDS.begin(), SUPPORTED_PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "Logitech G522 LIGHTSPEED"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_INACTIVE_TIME) | B(CAP_MICROPHONE_MUTE_LED_BRIGHTNESS);
    }

    constexpr capability_detail getCapabilityDetail(enum capabilities cap) const override
    {
        switch (cap) {
        case CAP_BATTERY_STATUS:
        case CAP_SIDETONE:
        case CAP_INACTIVE_TIME:
        case CAP_MICROPHONE_MUTE_LED_BRIGHTNESS:
            return { .usagepage = 0xffa0, .usageid = 0x0001, .interface_id = 3 };
        default:
            return HIDDevice::getCapabilityDetail(cap);
        }
    }

    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        auto start_time = std::chrono::steady_clock::now();

        std::array<uint8_t, PACKET_SIZE> request = buildBatteryRequest();
        if (auto write_result = writeHID(device_handle, request, PACKET_SIZE); !write_result) {
            return write_result.error();
        }

        std::vector<uint8_t> raw_packets;
        raw_packets.reserve(PACKET_SIZE * 4);

        for (int attempt = 0; attempt < 4; ++attempt) {
            std::array<uint8_t, PACKET_SIZE> response { };
            auto read_result = readHIDTimeout(device_handle, response, hsc_device_timeout);
            if (!read_result) {
                return read_result.error();
            }

            raw_packets.insert(raw_packets.end(), response.begin(), response.end());

            if (isPowerOffPacket(response)) {
                return DeviceError::deviceOffline("Headset is powered off or not connected");
            }

            if (isPowerEventPacket(response)) {
                continue;
            }

            if (isAckPacket(response)) {
                continue;
            }

            if (!isBatteryResponsePacket(response)) {
                continue;
            }

            auto battery_result = parseBatteryResponse(response);
            if (!battery_result) {
                return battery_result.error();
            }

            battery_result->raw_data       = std::move(raw_packets);
            auto end_time                  = std::chrono::steady_clock::now();
            battery_result->query_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            return *battery_result;
        }

        return DeviceError::protocolError("Battery response packet not received");
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        // INFO: The original G HUB app does some strange mapping:
        //   0 -   5 -> 0x00 (off)
        //   6 -  16 -> 0x01
        //  17 -  27 -> 0x02
        //  28 -  38 -> 0x03
        //  39 -  49 -> 0x04
        //  50 -  61 -> 0x05
        //  62 -  72 -> 0x06
        //  73 -  83 -> 0x07
        //  84 -  94 -> 0x08
        //  95 - 100 -> 0x09
        uint8_t mapped = map<uint8_t>(level, 0, 128, 0, 9);

        auto command = buildSidetoneCommand(mapped);
        if (auto write_result = writeHID(device_handle, command, PACKET_SIZE); !write_result) {
            return write_result.error();
        }

        return SidetoneResult {
            .current_level = level,
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = 0,
            .device_max    = 9
        };
    }

    Result<InactiveTimeResult> setInactiveTime(hid_device* device_handle, uint8_t minutes) override
    {
        auto command = buildInactiveTimeCommand(minutes);
        if (auto write_result = writeHID(device_handle, command, PACKET_SIZE); !write_result) {
            return write_result.error();
        }

        return InactiveTimeResult {
            .minutes     = minutes,
            .min_minutes = 0,
            .max_minutes = 90
        };
    }

    Result<MicMuteLedBrightnessResult> setMicMuteLedBrightness(hid_device* device_handle, uint8_t brightness) override
    {
        uint8_t mute_led = static_cast<uint8_t>(static_cast<bool>(brightness)); // 0 or 1
        auto command     = buildMicMuteLedCommand(mute_led);
        if (auto write_result = writeHID(device_handle, command, PACKET_SIZE); !write_result) {
            return write_result.error();
        }

        return MicMuteLedBrightnessResult {
            .brightness     = mute_led,
            .min_brightness = 0,
            .max_brightness = 1
        };
    }

    static constexpr bool isAckPacket(std::span<const uint8_t> packet)
    {
        return packet.size() >= 3 && packet[0] == REPORT_PREFIX[0] && packet[1] == REPORT_PREFIX[1] && packet[2] == 0x03;
    }

    static constexpr bool isPowerOffPacket(std::span<const uint8_t> packet)
    {
        return packet.size() >= 7 && packet[0] == REPORT_PREFIX[0] && packet[1] == REPORT_PREFIX[1] && packet[2] == 0x05 && packet[7] == 0x00;
    }

    static constexpr bool isPowerEventPacket(std::span<const uint8_t> packet)
    {
        return packet.size() >= 3 && packet[0] == REPORT_PREFIX[0] && packet[1] == REPORT_PREFIX[1] && packet[2] == 0x05;
    }

    static constexpr bool isBatteryResponsePacket(std::span<const uint8_t> packet)
    {
        return packet.size() >= 14 && packet[0] == REPORT_PREFIX[0] && packet[1] == REPORT_PREFIX[1] && packet[2] == 0x0b && packet[9] == 0x05;
    }

    static Result<BatteryResult> parseBatteryResponse(std::span<const uint8_t> packet)
    {
        if (!isBatteryResponsePacket(packet)) {
            return DeviceError::protocolError("Unexpected battery response packet");
        }

        int level = static_cast<int>(packet[11]);
        if (level > 100) {
            return DeviceError::protocolError("Battery percentage out of range");
        }

        auto status = packet[13] == 0x02 ? BATTERY_CHARGING : BATTERY_AVAILABLE;

        BatteryResult result {
            .level_percent = level,
            .status        = status,
        };

        return result;
    }

private:
    static constexpr std::array<uint8_t, PACKET_SIZE> buildBatteryRequest()
    {
        std::array<uint8_t, PACKET_SIZE> request { };
        request[0]  = REPORT_PREFIX[0];
        request[1]  = REPORT_PREFIX[1];
        request[2]  = 0x0b;
        request[4]  = 0x03;
        request[5]  = 0x1a;
        request[7]  = 0x03;
        request[9]  = 0x05;
        request[10] = 0x0a;
        return request;
    }

    static constexpr std::array<uint8_t, PACKET_SIZE> buildSidetoneCommand(uint8_t level)
    {
        std::array<uint8_t, PACKET_SIZE> command { };
        command[0]  = REPORT_PREFIX[0];
        command[1]  = REPORT_PREFIX[1];
        command[2]  = 0x0b;
        command[4]  = 0x03;
        command[5]  = 0x1c;
        command[7]  = 0x06;
        command[9]  = 0x0d;
        command[10] = 0x1c;
        command[11] = 0x01;
        command[12] = 0xff;
        command[13] = level;
        return command;
    }

    static constexpr std::array<uint8_t, PACKET_SIZE> buildInactiveTimeCommand(uint8_t minutes)
    {
        std::array<uint8_t, PACKET_SIZE> command { };
        command[0]  = REPORT_PREFIX[0];
        command[1]  = REPORT_PREFIX[1];
        command[2]  = 0x0b;
        command[4]  = 0x03;
        command[5]  = 0x1c;
        command[7]  = 0x06;
        command[9]  = 0x14;
        command[10] = 0x1c;
        command[11] = minutes;

        // WARN: This has a side effect since there are multiple timers being set with the same command.
        // command[12] = 0x00; // This byte sets the time until "lighting goes into inactive mode" e.g. dimmer lights, etc. (can be set in G HUB).
        // command[13] = 0x00; // This byte sets the time until "lighting off because of inactivity"
        // For both timers, a value of 0 is labeled "never" in G HUB

        return command;
    }

    static constexpr std::array<uint8_t, PACKET_SIZE> buildMicMuteLedCommand(uint8_t mute_led)
    {
        std::array<uint8_t, PACKET_SIZE> command { };
        command[0]  = REPORT_PREFIX[0];
        command[1]  = REPORT_PREFIX[1];
        command[2]  = 0x09;
        command[4]  = 0x03;
        command[5]  = 0x1c;
        command[7]  = 0x04;
        command[9]  = 0x15;
        command[10] = 0x2c;
        command[11] = mute_led;
        return command;
    }
};

} // namespace headsetcontrol
