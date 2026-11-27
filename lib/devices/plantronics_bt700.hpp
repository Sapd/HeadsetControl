#pragma once

#include "protocols/poly_protocol.hpp"
#include <array>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Poly Voyager Focus 2 (via BT700 USB dongle)
 *
 * Same features and group/item ids as the BT600: sidetone, battery status,
 * lights (the online indicator), voice prompts and volume limiter. See
 * protocols::PolyProtocol for the wire format. The 0xFFA2 collection is on
 * interface 3 as on the BT600 (the dongle's only HID interface; 0-2 are
 * audio), so the inherited getCapabilityDetail() fits. Verified on Linux and
 * Windows 11 with a Voyager Focus 2 (dongle firmware 4724, headset 5595).
 *
 * Differences from the BT600:
 * - The descriptor declares OUTPUT report 0x07 as 504 bytes and the dongle
 *   stalls a shorter SET_REPORT (EPIPE), so frames are padded to that size.
 * - The 06 01 output report sent before the hellos is not needed, and report
 *   0x06 belongs to another top-level collection (0xFFA0), so on Windows
 *   writing it to this one fails ("Incorrect function"). It is skipped.
 */
class PlantronicsBT700 : public protocols::PolyProtocol {
public:
    static constexpr size_t OUT_SIZE = 504;

    static constexpr std::array<uint16_t, 1> PRODUCT_IDS {
        0x02E6 // Poly BT700 dongle (Voyager Focus 2)
    };

    constexpr std::vector<uint16_t> getProductIds() const override
    {
        return { PRODUCT_IDS.begin(), PRODUCT_IDS.end() };
    }

    constexpr std::string_view getDeviceName() const override
    {
        return "Poly Voyager Focus 2 (BT700)"sv;
    }

    constexpr uint8_t getSupportedPlatforms() const override
    {
        // Tested on Linux and Windows 11. Untested on macOS, see the BT600.
        return PLATFORM_LINUX | PLATFORM_WINDOWS;
    }

protected:
    size_t getOutputReportSize() const override
    {
        return OUT_SIZE;
    }

    bool needsSessionNudge() const override
    {
        return false;
    }
};

} // namespace headsetcontrol
