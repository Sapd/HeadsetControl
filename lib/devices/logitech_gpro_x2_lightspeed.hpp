#pragma once

#include "../utility.hpp"
#include "hid_device.hpp"
#include <array>
#include <chrono>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Logitech G PRO X 2 LIGHTSPEED (PID 0x0af7)
 *
 * This variant uses a vendor-specific 64-byte protocol on usage page 0xffa0
 * for battery data instead of HID++.
 */
class LogitechGProX2Lightspeed : public HIDDevice {
public:
    static constexpr std::array<uint16_t, 1> SUPPORTED_PRODUCT_IDS { 0x0af7 };
    static constexpr size_t PACKET_SIZE                                        = 64;
    static constexpr uint8_t REPORT_PREFIX                                     = 0x51;

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
        return "Logitech G PRO X 2 LIGHTSPEED"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_INACTIVE_TIME);
    }

    constexpr capability_detail getCapabilityDetail(enum capabilities cap) const override
    {
        switch (cap) {
        case CAP_BATTERY_STATUS:
        case CAP_SIDETONE:
        case CAP_INACTIVE_TIME:
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
            std::array<uint8_t, PACKET_SIZE> response {};
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
        uint8_t mapped = map<uint8_t>(level, 0, 128, 0, 100);

        auto command = buildSidetoneCommand(mapped);
        if (auto write_result = writeHID(device_handle, command, PACKET_SIZE); !write_result) {
            return write_result.error();
        }

        return SidetoneResult {
            .current_level = level,
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = 0,
            .device_max    = 100
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

    static constexpr bool isAckPacket(std::span<const uint8_t> packet)
    {
        return packet.size() >= 2 && packet[0] == REPORT_PREFIX && packet[1] == 0x03;
    }

    static constexpr bool isPowerOffPacket(std::span<const uint8_t> packet)
    {
        return packet.size() >= 7 && packet[0] == REPORT_PREFIX && packet[1] == 0x05 && packet[6] == 0x00;
    }

    static constexpr bool isPowerEventPacket(std::span<const uint8_t> packet)
    {
        return packet.size() >= 2 && packet[0] == REPORT_PREFIX && packet[1] == 0x05;
    }

    static constexpr bool isBatteryResponsePacket(std::span<const uint8_t> packet)
    {
        return packet.size() >= 13 && packet[0] == REPORT_PREFIX && packet[1] == 0x0b && packet[8] == 0x04;
    }

    static Result<BatteryResult> parseBatteryResponse(std::span<const uint8_t> packet)
    {
        if (!isBatteryResponsePacket(packet)) {
            return DeviceError::protocolError("Unexpected battery response packet");
        }

        int level = static_cast<int>(packet[10]);
        if (level > 100) {
            return DeviceError::protocolError("Battery percentage out of range");
        }

        auto status = packet[12] == 0x02 ? BATTERY_CHARGING : BATTERY_AVAILABLE;

        BatteryResult result {
            .level_percent = level,
            .status        = status,
        };

        return result;
    }

private:
    static constexpr std::array<uint8_t, PACKET_SIZE> buildBatteryRequest()
    {
        std::array<uint8_t, PACKET_SIZE> request {};
        request[0] = REPORT_PREFIX;
        request[1] = 0x08;
        request[3] = 0x03;
        request[4] = 0x1a;
        request[6] = 0x03;
        request[8] = 0x04;
        request[9] = 0x0a;
        return request;
    }

    static constexpr std::array<uint8_t, PACKET_SIZE> buildSidetoneCommand(uint8_t level)
    {
        std::array<uint8_t, PACKET_SIZE> command {};
        command[0]  = REPORT_PREFIX;
        command[1]  = 0x0a;
        command[3]  = 0x03;
        command[4]  = 0x1b;
        command[6]  = 0x03;
        command[8]  = 0x07;
        command[9]  = 0x1b;
        command[10] = level;
        return command;
    }

    static constexpr std::array<uint8_t, PACKET_SIZE> buildInactiveTimeCommand(uint8_t minutes)
    {
        std::array<uint8_t, PACKET_SIZE> command {};
        command[0]  = REPORT_PREFIX;
        command[1]  = 0x09;
        command[3]  = 0x03;
        command[4]  = 0x1c;
        command[6]  = 0x03;
        command[8]  = 0x06;
        command[9]  = 0x1d;
        command[10] = minutes;
        return command;
    }
};

} // namespace headsetcontrol
