#pragma once

#include "../result_types.hpp"
#include "hid_device.hpp"
#include <array>
#include <string_view>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief HyperX Cloud 3 Wired Gaming Headset
 *
 * Features:
 * - Sidetone control (on/off only)
 *
 * Simple wired headset with basic sidetone toggle
 */
class HyperXCloud3 : public HIDDevice {
public:
    static constexpr uint16_t VENDOR_HYPERX_CLOUD = 0x03f0;
    static constexpr std::array<uint16_t, 1> SUPPORTED_PRODUCT_IDS {
        0x089d // Cloud 3 Wired
    };

    static constexpr int MSG_SIZE = 62;

    constexpr uint16_t getVendorId() const override
    {
        return VENDOR_HYPERX_CLOUD;
    }

    std::vector<uint16_t> getProductIds() const override
    {
        return { SUPPORTED_PRODUCT_IDS.begin(), SUPPORTED_PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "HyperX Cloud 3"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_SIDETONE);
    }

    // Rich Results V2 API
    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        // Cloud 3 only supports on/off
        uint8_t on = (level > 0) ? 1 : 0;

        std::array<uint8_t, MSG_SIZE> cmd {};
        cmd[0] = 0x20;
        cmd[1] = 0x86;
        cmd[2] = on;

        if (auto result = sendFeatureReport(device_handle, cmd); !result) {
            return result.error();
        }

        return SidetoneResult {
            .current_level = level,
            .min_level     = 0,
            .max_level     = 1, // Only on/off
            .device_min    = 0,
            .device_max    = 1
        };
    }
};
} // namespace headsetcontrol
