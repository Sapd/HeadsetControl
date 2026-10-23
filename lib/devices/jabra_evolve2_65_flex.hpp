#pragma once

#include "protocols/jabra_gnp_protocol.hpp"
#include <array>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Jabra Evolve2 65 Flex connected by its USB cable
 *
 * Same features and protocol as through the Link 390 dongle; the headset answers at
 * GNP address 0x08 when it is on USB itself, and its vendor collection uses usage 0x01.
 * Verified on Linux and macOS with an Evolve2 65 Flex (0x2519, fw 1.2.14).
 */
class JabraEvolve265Flex : public protocols::JabraGNPDevice<0x08, 0x01> {
public:
    static constexpr std::array<uint16_t, 1> PRODUCT_IDS { 0x2519 };

    constexpr std::vector<uint16_t> getProductIds() const override
    {
        return { PRODUCT_IDS.begin(), PRODUCT_IDS.end() };
    }

    constexpr std::string_view getDeviceName() const override
    {
        return "Jabra Evolve2 65 Flex (USB)"sv;
    }

    // Windows is left out because it is untested, not because the protocol needs
    // anything different there: getCapabilityDetail() reports usagepage/usageid.
    constexpr uint8_t getSupportedPlatforms() const override
    {
        return PLATFORM_LINUX | PLATFORM_MACOS;
    }
};

} // namespace headsetcontrol
