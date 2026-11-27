#pragma once

#include "protocols/poly_protocol.hpp"
#include <array>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Plantronics/Poly Voyager 8200 UC (via BT600 USB dongle)
 *
 * Features: sidetone (3 levels), battery status with remaining talk time,
 * lights (the online indicator), voice prompts (the notification tones
 * setting) and volume limiter (the noise exposure limit). See
 * protocols::PolyProtocol for the wire format.
 *
 * Validated live against the hardware (firmware v2120).
 */
class PlantronicsBT600 : public protocols::PolyProtocol {
public:
    static constexpr std::array<uint16_t, 1> PRODUCT_IDS {
        0x02EE // Plantronics BT600 dongle (Voyager 8200 UC)
    };

    constexpr std::vector<uint16_t> getProductIds() const override
    {
        return { PRODUCT_IDS.begin(), PRODUCT_IDS.end() };
    }

    constexpr std::string_view getDeviceName() const override
    {
        return "Plantronics Voyager 8200 UC (BT600)"sv;
    }

    constexpr uint8_t getSupportedPlatforms() const override
    {
        // Untested on macOS: the dongle exposes five top-level HID
        // collections and macOS opens only the first enumerated one, which
        // may not be the Poly control collection (see get_hid_path).
        return PLATFORM_LINUX | PLATFORM_WINDOWS;
    }
};

} // namespace headsetcontrol
