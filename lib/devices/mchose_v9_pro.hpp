#pragma once

#include "mchose_x9.hpp"

#include <array>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief MCHOSE V9 Pro Wireless Gaming Headset
 *
 * Features:
 * - Battery status
 *
 * Same "M HUB" protocol as the X9 (see MchoseX9), but the V9 Pro's 2.4GHz
 * dongle enumerates under a different USB vendor id: 291d:385d, product
 * string "C-Media Electronics Inc MCHOSE V9 PRO".
 *
 * Verified on Linux with the real dongle: its report descriptor declares the
 * same vendor collection as the X9 (usage page 0xff90, report 0x55, 63 data
 * bytes in and out), and a 64-byte write of 55 65 01 was answered with
 * 55 65 32 02, i.e. 50%, matching the headset at that moment.
 */
class MchoseV9Pro : public MchoseX9 {
public:
    static constexpr uint16_t VENDOR_MCHOSE_V9_PRO = 0x291d;
    static constexpr std::array<uint16_t, 1> V9_PRO_PRODUCT_IDS { 0x385d };

    constexpr uint16_t getVendorId() const override
    {
        return VENDOR_MCHOSE_V9_PRO;
    }

    std::vector<uint16_t> getProductIds() const override
    {
        return { V9_PRO_PRODUCT_IDS.begin(), V9_PRO_PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "MCHOSE V9 Pro Wireless"sv;
    }
};

} // namespace headsetcontrol
