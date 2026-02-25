#pragma once
#include "../result_types.hpp"
#include "../utility.hpp"
#include "corsair_device.hpp"
#include "device.hpp"
#include "device_utils.hpp"
#include "hidapi.h"
#include <array>
#include <chrono>
#include <cstdint>
#include <string_view>
#include <thread>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Corsair Void Wireless V2
 *
 * This implementation extracts the following data from HID packets:
 * - Battery percentage (0-100%) which seems to be reliable up to an
 *   approximate 10% error
 * - Device status (connected/disconnected)
 * - Sidetone with level mapping (0-128 to device-specific range)
 * - Query timing
 */
class CorsairVoidV2W : public CorsairDevice {
public:
    static constexpr std::array<uint16_t, 1> SUPPORTED_PRODUCT_IDS { 0x2a08 };

    std::vector<uint16_t> getProductIds() const override
    {
        return { SUPPORTED_PRODUCT_IDS.begin(), SUPPORTED_PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "Corsair Wireless V2 Headset Device"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS);
    }
    // Override capability as this device needs the interface_id = 4
    constexpr capability_detail
    getCapabilityDetail(enum capabilities cap) const override
    {
        return { .usagepage = 0, .usageid = 0, .interface_id = 4 };
    }

    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        auto init_result = initializeDevice(device_handle);
        if (init_result.valueOr(0) == 0) {
            return DeviceError::deviceOffline(
                "Headset not connected to wireless receiver");
        }

        if (auto result = flushHIDBuffer(device_handle); !result) {
            return result.error();
        }

        // Send battery status request
        std::array<uint8_t, MSG_SIZE_WRITE> battery_request {
            0x00, 0x02, HEADSET_ENDPOINT, 0x02, 0x0f
        };

        if (auto result = writeHID(device_handle, battery_request, MSG_SIZE_WRITE);
            !result) {
            return result.error();
        }
        std::array<uint8_t, MSG_SIZE_READ> battery_response {};
        auto battery_read_result = readHIDTimeout(device_handle, battery_response, hsc_device_timeout);

        if (!battery_read_result) {
            return battery_read_result.error();
        }
        // Parse the Corsair battery packet
        // Packet format:
        // [4] low byte of 1-1000 battery value
        // [5] high byte of 1-1000 battery value

        uint8_t low_byte  = battery_response[4];
        uint8_t high_byte = battery_response[5];

        enum battery_status status = BATTERY_UNAVAILABLE;

        // Extract battery level (0-100%)
        // for some odd reason it doesn't always report the right value and
        // instead reports this value which is the productID + 1??
        if (high_byte == 0x2a) {
            // try again
            if (auto result = writeHID(device_handle, battery_request, MSG_SIZE_WRITE);
                !result) {
                return result.error();
            }
            battery_read_result = readHIDTimeout(device_handle, battery_response, hsc_device_timeout);
            if (!battery_read_result) {
                return battery_read_result.error();
            }
            low_byte  = battery_response[4];
            high_byte = battery_response[5];
            if (high_byte == 0x2a) {

                BatteryResult result {
                    .level_percent = -1,
                    .status        = status,
                    .raw_data      = std::vector<uint8_t>(battery_response.begin(),
                             battery_response.end()),
                };
            }
        }
        status                        = BATTERY_AVAILABLE;
        uint16_t battery_level_vendor = (high_byte << 8) | low_byte;

        int level = static_cast<int>(battery_level_vendor / 10);

        // if there's no sidetone, the mic could be muted

        // Build result
        BatteryResult result {
            .level_percent = level,
            .status        = status,
            .raw_data      = std::vector<uint8_t>(battery_response.begin(),
                     battery_response.end()),
        };
        return result;
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle,
        uint8_t level) override
    {
        // Corsair uses range 0-1000 internally in steps of 10
        constexpr uint8_t CORSAIR_MIN  = 0;
        constexpr uint16_t CORSAIR_MAX = 1000;

        // Map from 0-128 to 200-255
        uint16_t mapped_level   = map(level, 0, 128, CORSAIR_MIN, CORSAIR_MAX);
        uint16_t sidetone_value = round_to_multiples(mapped_level, 10);
        uint8_t low_byte        = sidetone_value & 0xFF;
        uint8_t high_byte       = (sidetone_value >> 8) & 0xFF;
        auto init_result        = initializeDevice(device_handle);
        if (init_result.valueOr(0) == 0) {
            return DeviceError::deviceOffline(
                "Headset not connected to wireless receiver");
        }

        // turn off ANC if sidetone is to be enabled
        std::array<uint8_t, MSG_SIZE_WRITE> ANC_toggle { 0x00, 0x02, HEADSET_ENDPOINT,
            0x01, 0xd1 };
        std::array<uint8_t, MSG_SIZE_WRITE> sidetone_toggle {
            0x00, 0x02, HEADSET_ENDPOINT, 0x01, 0x46
        };

        if (level == 0) {
            // turn off sidetone
            sidetone_toggle[6] = 0x01;
            // turn on noice cancellation which has to be off for sidetone to work
            ANC_toggle[6] = 0x01;
        }
        auto ANC_result = writeHID(device_handle, ANC_toggle, MSG_SIZE_WRITE);
        if (!ANC_result) {
            return ANC_result.error();
        }
        auto sidetone_result = writeHID(device_handle, sidetone_toggle, MSG_SIZE_WRITE);
        if (!sidetone_result) {
            return sidetone_result.error();
        }

        std::array<uint8_t, MSG_SIZE_WRITE> sidetone_data {
            0x00, 0x02, HEADSET_ENDPOINT, 0x01, 0x47, 0, low_byte, high_byte
        };

        if (auto result = writeHID(device_handle, sidetone_data, MSG_SIZE_WRITE);
            !result) {
            return result.error();
        }

        return SidetoneResult {
            .current_level = level,
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = CORSAIR_MIN,
            .device_max    = 100, // should be 1000 but the structure doesn't allow 2
                               // byte numbers
        };
    }

    Result<CapabilityInfo> getCapabilityInfo(enum capabilities cap) override
    {
        auto info = HIDDevice::getCapabilityInfo(cap);
        if (!info)
            return info;

        // Add device-specific parameter info
        switch (cap) {
        case CAP_SIDETONE:
            info->parameter = CapabilityInfo::RangeParam {
                .min = 0, .max = 128, .step = 1, .units = "level"
            };
            break;

        default:
            break;
        }

        return info;
    }

private:
    static constexpr uint8_t RECEIVER_ENDPOINT = 0x08;
    static constexpr uint8_t HEADSET_ENDPOINT  = 0x09;
    static constexpr uint8_t MSG_SIZE_READ     = 64;
    static constexpr uint8_t MSG_SIZE_WRITE    = 65;

    Result<size_t> initializeDevice(hid_device* device_handle) const
    {
        // get firmware of receiver; neccessary for sending commands
        std::array<uint8_t, MSG_SIZE_WRITE> firmware_data {
            0x00, 0x02, RECEIVER_ENDPOINT, 0x02, 0x13
        };
        if (auto result = writeHID(device_handle, firmware_data, MSG_SIZE_WRITE);
            !result) {
            return result.error();
        }

        // turn on software mode for the receiver; neccessary for sending commands
        std::array<uint8_t, MSG_SIZE_WRITE> software_mode_data {
            0x00, 0x02, RECEIVER_ENDPOINT, 0x01, 0x03, 0x00, 0x02
        };

        if (auto result = writeHID(device_handle, software_mode_data, MSG_SIZE_WRITE);
            !result) {
            return result.error();
        }

        // send heartbeat to receiver; neccessary for sending commands
        std::array<uint8_t, MSG_SIZE_WRITE> heartbeat_data {
            0x00, 0x02, RECEIVER_ENDPOINT, 0x02, 0x12
        };

        if (auto result = writeHID(device_handle, heartbeat_data, MSG_SIZE_WRITE);
            !result) {
            return result.error();
        }

        // repeat commands for headset
        software_mode_data[2] = HEADSET_ENDPOINT;
        heartbeat_data[2]     = HEADSET_ENDPOINT;
        if (auto result = writeHID(device_handle, software_mode_data, MSG_SIZE_WRITE);
            !result) {
            return result.error();
        }

        if (auto result = flushHIDBuffer(device_handle); !result) {
            return result.error();
        }

        if (auto result = writeHID(device_handle, heartbeat_data, MSG_SIZE_WRITE);
            !result) {
            return result.error();
        }
        std::array<uint8_t, MSG_SIZE_READ> heartbeat_response {};
        auto read_headset_result = readHIDTimeout(device_handle, heartbeat_response, hsc_device_timeout);
        return read_headset_result;
    }

    Result<void> flushHIDBuffer(hid_device* device_handle) const
    {
        std::array<uint8_t, MSG_SIZE_READ> flush_buffer {};
        Result<size_t> data_flush_read = readHIDTimeout(device_handle, flush_buffer, 5);
        while (true) {
            if (data_flush_read.hasError()) {
                if (data_flush_read.error().code == DeviceError::Code::Timeout) {
                    return {};
                }
                return data_flush_read.error();
            }
            data_flush_read = readHIDTimeout(device_handle, flush_buffer, 5);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
};
} // namespace headsetcontrol
