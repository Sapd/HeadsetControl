#pragma once

#include "protocols/jabra_gnp_protocol.hpp"
#include <array>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Jabra Evolve3 75 connected by its USB cable
 *
 * Same GN Protocol (GNP) as the Evolve2 65 Flex on USB: the headset answers at GNP
 * address 0x08. Its vendor collection (usage page 0xFF00) carries report 5 on usage
 * 0x05, like the Link 390. Verified on Windows with an Evolve3 75 MS (0x2548):
 * battery, sidetone, busylight, voice prompts, inactivity and IntelliTone all answer.
 *
 * PIDs: 0x2546 UC, 0x2547 Data, 0x2548 MS. Jabra Direct lists all three with the same
 * firmware (DFU PID 0x2546); only 0x2548 is tested.
 */
class JabraEvolve375 : public protocols::JabraGNPDevice<0x08, 0x05> {
public:
    static constexpr std::array<uint16_t, 3> PRODUCT_IDS { 0x2546, 0x2547, 0x2548 };

    constexpr std::vector<uint16_t> getProductIds() const override
    {
        return { PRODUCT_IDS.begin(), PRODUCT_IDS.end() };
    }

    constexpr std::string_view getDeviceName() const override
    {
        return "Jabra Evolve3 75 (USB)"sv;
    }
};

} // namespace headsetcontrol
