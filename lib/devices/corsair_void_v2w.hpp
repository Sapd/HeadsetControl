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
 * @brief Corsair Wireless V2 headsets
 *
 * This implementation extracts the following data from HID packets:
 * - Battery percentage (0-100%) which seems to be reliable up to an approximate 10% error
 * - Device status (connected/disconnected)
 * - Sidetone with level mapping (0-128 to device-specific range)
 * - Inactive timer (0-90 minutes range)
 * - Microphone "up" status
 * - Query timing
 */
class CorsairVoidV2W : public CorsairDevice {
public:
    static constexpr std::array<uint16_t, 3> SUPPORTED_PRODUCT_IDS {
        0x2a08, // VOID WIRELESS V2 (receiver)
        0x2a02, // VIRTUOSO MAX WIRELESS (receiver)
        0x0a97  // HS80 MAX Wireless (receiver)
    };

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
        return B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_INACTIVE_TIME);
    }

    // Override capability as this device needs the interface_id = 4
    constexpr capability_detail
    getCapabilityDetail(enum capabilities cap) const override
    {
        return { .usagepage = 0, .usageid = 0, .interface_id = 4 };
    }

    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        // Battery can be queried without taking over audio control
        auto init_result = wakeDevice(device_handle);
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

        if (auto result = writeHID(device_handle, battery_request, MSG_SIZE_WRITE); !result) {
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

        // The receiver send packets with the paired headset ID instead of the
        // battery level. Maybe related to multiple headset paired at the same time?
        // We just re-request until we get a plausible reading...

        enum battery_status status = BATTERY_UNAVAILABLE;

        uint16_t battery_level_vendor = static_cast<uint16_t>(battery_response[4] | (battery_response[5] << 8));

        const uint16_t battery_level_vendor_max = 1000;
        const int battery_level_max_attempts = 3;

        for (int attempt = 0; attempt < battery_level_max_attempts &&
             (battery_level_vendor == 0 || battery_level_vendor > battery_level_vendor_max); ++attempt) {
            if (auto result = writeHID(device_handle, battery_request, MSG_SIZE_WRITE); !result) {
                return result.error();
            }
            battery_read_result = readHIDTimeout(device_handle, battery_response, hsc_device_timeout);
            if (!battery_read_result) {
                return battery_read_result.error();
            }
            battery_level_vendor = static_cast<uint16_t>(battery_response[4] | (battery_response[5] << 8));
        }

        // Still out of range? report unavailable
        if (battery_level_vendor == 0 || battery_level_vendor > battery_level_vendor_max) {
            return BatteryResult {
                .level_percent = -1,
                .status        = BATTERY_UNAVAILABLE,
                .raw_data      = std::vector<uint8_t>(battery_response.begin(), battery_response.end()),
            };
        }

        status = BATTERY_AVAILABLE;
        int level = static_cast<int>(battery_level_vendor / 10);

        // Get the microphone mute state
        // These headsets physically flip the boom mic up to mute, so we map muted as MICROPHONE_UP
        enum microphone_status mic_status = MICROPHONE_UNKNOWN;

        // Send mic status request
        std::array<uint8_t, MSG_SIZE_WRITE> mic_request {
            0x00, 0x02, HEADSET_ENDPOINT, 0x02, 0xa6
        };

        if (auto write_result = writeHID(device_handle, mic_request, MSG_SIZE_WRITE); write_result) {
            std::array<uint8_t, MSG_SIZE_READ> mic_response {};
            if (auto mic_read = readHIDTimeout(device_handle, mic_response, hsc_device_timeout);
                mic_read && mic_response[4] == 0x01) { // muted
                mic_status = MICROPHONE_UP;
            }
        }

        // Build result
        BatteryResult result {
            .level_percent = level,
            .status        = status,
            .mic_status    = mic_status,
            .raw_data      = std::vector<uint8_t>(battery_response.begin(), battery_response.end()),
        };
        return result;
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
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
            return DeviceError::deviceOffline("Headset not connected to wireless receiver");
        }

        // Turn off ANC if sidetone is to be enabled
        std::array<uint8_t, MSG_SIZE_WRITE> ANC_toggle { 0x00, 0x02, HEADSET_ENDPOINT,
            0x01, 0xd1 };
        std::array<uint8_t, MSG_SIZE_WRITE> sidetone_toggle {
            0x00, 0x02, HEADSET_ENDPOINT, 0x01, 0x46
        };

        if (level == 0) {
            // Turn off sidetone
            sidetone_toggle[6] = 0x01;
            // Turn on noice cancellation which has to be off for sidetone to work
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

        if (auto result = writeHID(device_handle, sidetone_data, MSG_SIZE_WRITE); !result) {
            return result.error();
        }

        return SidetoneResult {
            .current_level = level,
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = CORSAIR_MIN,
            .device_max    = 100, // should be 1000 but the structure doesn't allow 2 byte numbers
        };
    }

    Result<InactiveTimeResult> setInactiveTime(hid_device* device_handle, uint8_t minutes) override
    {
        constexpr uint8_t MIN_MINUTES = 0;
        constexpr uint8_t MAX_MINUTES = 90;

        if (minutes > MAX_MINUTES) {
            minutes = MAX_MINUTES;
        }

        auto init_result = initializeDevice(device_handle);
        if (init_result.valueOr(0) == 0) {
            return DeviceError::deviceOffline("Headset not connected to wireless receiver");
        }

        // Open the sleep-timer write endpoint (0x01 to enables the timer, 0x00 to disables it)
        std::array<uint8_t, MSG_SIZE_WRITE> open_sleep_endpoint {
            0x00, 0x02, HEADSET_ENDPOINT, 0x01, 0x0d, 0x00,
            static_cast<uint8_t>(minutes == 0 ? 0x00 : 0x01)
        };
        if (auto result = writeHID(device_handle, open_sleep_endpoint, MSG_SIZE_WRITE); !result) {
            return result.error();
        }

        // Set the idle timeout (in milliseconds)
        if (minutes > 0) {
            uint32_t timeout_ms = static_cast<uint32_t>(minutes) * 60U * 1000U;
            std::array<uint8_t, MSG_SIZE_WRITE> sleep_timer {
                0x00, 0x02, HEADSET_ENDPOINT, 0x01, 0x0e, 0x00,
                static_cast<uint8_t>(timeout_ms & 0xFF),
                static_cast<uint8_t>((timeout_ms >> 8) & 0xFF),
                static_cast<uint8_t>((timeout_ms >> 16) & 0xFF),
                static_cast<uint8_t>((timeout_ms >> 24) & 0xFF)
            };
            if (auto result = writeHID(device_handle, sleep_timer, MSG_SIZE_WRITE); !result) {
                return result.error();
            }
        }

        return InactiveTimeResult {
            .minutes     = minutes,
            .min_minutes = MIN_MINUTES,
            .max_minutes = MAX_MINUTES,
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

        case CAP_INACTIVE_TIME:
            info->parameter = CapabilityInfo::RangeParam {
                .min = 0, .max = 90, .step = 1, .units = "minutes"
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

    Result<size_t> wakeDevice(hid_device* device_handle) const
    {
        // wakeDevice() performs the minimal handshake required to talk to the headset
        // WITHOUT switching it into software mode. It avoid producing an audible 'pop'
        // sound while the device switches modes, and still let us read battery level.

        // Get firmware of receiver
        std::array<uint8_t, MSG_SIZE_WRITE> firmware_data {
            0x00, 0x02, RECEIVER_ENDPOINT, 0x02, 0x13
        };
        if (auto result = writeHID(device_handle, firmware_data, MSG_SIZE_WRITE); !result) {
            return result.error();
        }

        // Heartbeat the receiver
        std::array<uint8_t, MSG_SIZE_WRITE> heartbeat_data {
            0x00, 0x02, RECEIVER_ENDPOINT, 0x02, 0x12
        };
        if (auto result = writeHID(device_handle, heartbeat_data, MSG_SIZE_WRITE); !result) {
            return result.error();
        }
        if (auto result = flushHIDBuffer(device_handle); !result) {
            return result.error();
        }

        // Then heartbeat the headset
        heartbeat_data[2] = HEADSET_ENDPOINT;
        if (auto result = writeHID(device_handle, heartbeat_data, MSG_SIZE_WRITE); !result) {
            return result.error();
        }

        // Returns the headset heartbeat read result
        std::array<uint8_t, MSG_SIZE_READ> heartbeat_response {};
        auto read_headset_result = readHIDTimeout(device_handle, heartbeat_response, hsc_device_timeout);
        return read_headset_result;
    }

    Result<size_t> initializeDevice(hid_device* device_handle) const
    {
        // Get firmware of receiver; neccessary for sending commands
        std::array<uint8_t, MSG_SIZE_WRITE> firmware_data {
            0x00, 0x02, RECEIVER_ENDPOINT, 0x02, 0x13
        };
        if (auto result = writeHID(device_handle, firmware_data, MSG_SIZE_WRITE); !result) {
            return result.error();
        }

        // Turn on software mode for the receiver; neccessary for sending commands
        std::array<uint8_t, MSG_SIZE_WRITE> software_mode_data {
            0x00, 0x02, RECEIVER_ENDPOINT, 0x01, 0x03, 0x00, 0x02
        };

        if (auto result = writeHID(device_handle, software_mode_data, MSG_SIZE_WRITE); !result) {
            return result.error();
        }

        // Dend heartbeat to receiver; neccessary for sending commands
        std::array<uint8_t, MSG_SIZE_WRITE> heartbeat_data {
            0x00, 0x02, RECEIVER_ENDPOINT, 0x02, 0x12
        };

        if (auto result = writeHID(device_handle, heartbeat_data, MSG_SIZE_WRITE); !result) {
            return result.error();
        }

        // Repeat commands for headset
        software_mode_data[2] = HEADSET_ENDPOINT;
        heartbeat_data[2]     = HEADSET_ENDPOINT;
        if (auto result = writeHID(device_handle, software_mode_data, MSG_SIZE_WRITE); !result) {
            return result.error();
        }

        if (auto result = flushHIDBuffer(device_handle); !result) {
            return result.error();
        }

        if (auto result = writeHID(device_handle, heartbeat_data, MSG_SIZE_WRITE); !result) {
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
