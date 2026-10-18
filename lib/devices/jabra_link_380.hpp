#pragma once

#include "protocols/jabra_gnp_protocol.hpp"
#include <array>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Jabra Link 380 USB dongle, controlling the Jabra headset paired to it
 *
 * Same GN Protocol (GNP) as the Link 390; the headset is reached at GNP address
 * 0x04 and the dongle's vendor collection (usage page 0xFF00) carries report 5
 * on usage 0x01. Verified on Linux with a Link 380 (0x24c7, fw 1.16) paired
 * with a Jabra Evolve2 65.
 */
class JabraLink380 : public protocols::JabraGNPDevice<0x04, 0x01> {
public:
    static constexpr std::array<uint16_t, 1> PRODUCT_IDS { 0x24C7 };

    constexpr std::vector<uint16_t> getProductIds() const override
    {
        return { PRODUCT_IDS.begin(), PRODUCT_IDS.end() };
    }

    constexpr std::string_view getDeviceName() const override
    {
        return "Jabra Link 380 (paired headset)"sv;
    }

    // Windows is left out because it is untested, not because the protocol needs
    // anything different there: getCapabilityDetail() reports usagepage/usageid.
    constexpr uint8_t getSupportedPlatforms() const override
    {
        return PLATFORM_LINUX | PLATFORM_MACOS;
    }
};

} // namespace headsetcontrol
