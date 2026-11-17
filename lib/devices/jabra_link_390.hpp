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
 * on Link 390 fw 1.3.0, Linux and macOS, and with an Evolve3 75 on a Link 390 with
 * PID 0x2E51 on Windows (battery, sidetone read and write).
 *
 * Both PIDs expose the GNP vendor collection at usage page 0xFF00 / usage 0x05.
 */
class JabraLink390 : public protocols::JabraGNPDevice<0x04, 0x05> {
public:
    static constexpr std::array<uint16_t, 2> PRODUCT_IDS { 0x2E57, 0x2E51 };

    constexpr std::vector<uint16_t> getProductIds() const override
    {
        return { PRODUCT_IDS.begin(), PRODUCT_IDS.end() };
    }

    constexpr std::string_view getDeviceName() const override
    {
        return "Jabra Link 390 (paired headset)"sv;
    }
};

} // namespace headsetcontrol
