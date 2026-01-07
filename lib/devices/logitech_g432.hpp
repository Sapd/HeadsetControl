#pragma once

#include "protocols/hidpp_protocol.hpp"
#include <array>
#include <string_view>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Logitech G432/G433 Gaming Headsets
 *
 * These are simpler USB headsets with limited features.
 * Currently only sidetone is supported.
 *
 * Original implementation: logitech_g432.c (41 lines)
 * New implementation: ~35 lines (15% reduction, but cleaner structure)
 */
class LogitechG432 : public protocols::HIDPPDevice<LogitechG432> {
public:
    std::vector<uint16_t> getProductIds() const override
    {
        return {
            0x0a9c, // G432
            0x0a6d // G433
        };
    }

    std::string_view getDeviceName() const override
    {
        return "Logitech G432/G433"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_SIDETONE);
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        uint8_t mapped = map<uint8_t>(level, 0, 128, 0, 100);

        std::array<uint8_t, 3> cmd { 0x05, 0x1e, mapped };
        auto result = sendHIDPPFeature(device_handle, cmd);
        if (!result) {
            return result.error();
        }

        return SidetoneResult {
            .current_level = level,
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = 0,
            .device_max    = 100
        };
    }
};
} // namespace headsetcontrol
