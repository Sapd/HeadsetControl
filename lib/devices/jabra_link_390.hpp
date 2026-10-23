#pragma once

#include "protocols/jabra_gnp_protocol.hpp"
#include <array>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Jabra Link 390 USB dongle, controlling the Jabra headset paired to it
 *
 * Features: battery, sidetone (+status), busylight as lights, voice prompts, inactive
 * time, volume limiter (audio protection). Verified with an Evolve2 65 Flex (fw 1.2.14)
 * on Link 390 fw 1.3.0, Linux and macOS.
 */
class JabraLink390 : public protocols::JabraGNPDevice<0x04, 0x05> {
public:
    static constexpr std::array<uint16_t, 1> PRODUCT_IDS { 0x2E57 };

    constexpr std::vector<uint16_t> getProductIds() const override
    {
        return { PRODUCT_IDS.begin(), PRODUCT_IDS.end() };
    }

    constexpr std::string_view getDeviceName() const override
    {
        return "Jabra Link 390 (paired headset)"sv;
    }

    // Windows is left out because it is untested, not because the protocol needs
    // anything different there: getCapabilityDetail() reports usagepage/usageid.
    constexpr uint8_t getSupportedPlatforms() const override
    {
        return PLATFORM_LINUX | PLATFORM_MACOS;
    }
};

} // namespace headsetcontrol
