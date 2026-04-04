#pragma once

#include "../result_types.hpp"
#include "hid_device.hpp"
#include <array>
#include <string_view>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Sony INZONE Buds (WF-G700N) wireless gaming earbuds
 *
 * Communicates via USB dongle (VID 054c PID 0ec2).
 * The dongle sends unsolicited HID reports; battery data is in the report
 * identified by byte[1]=0x12 and byte[2]=0x04 (report ID byte[0]=0x02).
 *
 * Battery bytes (0-100, direct percentage):
 *   byte[14] = right earbud
 *   byte[16] = left earbud
 *   byte[18] = charging case
 *
 * Checksum: byte[19] = (byte[14] + byte[16] + 117) mod 256
 */
class SonyINZONEBuds : public HIDDevice {
public:
    static constexpr uint16_t VENDOR_SONY = 0x054c;
    static constexpr std::array<uint16_t, 1> PRODUCT_IDS { 0x0ec2 };

    static constexpr int     REPORT_SIZE       = 64;
    static constexpr uint8_t BATTERY_TYPE      = 0x12;
    static constexpr uint8_t BATTERY_SUBTYPE   = 0x04;
    static constexpr int     MAX_READ_ATTEMPTS = 45;

    static constexpr int BYTE_RIGHT_EARBUD = 14;
    static constexpr int BYTE_LEFT_EARBUD  = 16;
    static constexpr int BYTE_CASE         = 18;

    constexpr uint16_t getVendorId() const override { return VENDOR_SONY; }

    std::vector<uint16_t> getProductIds() const override
    {
        return { PRODUCT_IDS.begin(), PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override { return "Sony INZONE Buds"sv; }

    constexpr int getCapabilities() const override { return B(CAP_BATTERY_STATUS); }

    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        std::array<uint8_t, REPORT_SIZE> response {};

        for (int attempt = 0; attempt < MAX_READ_ATTEMPTS; ++attempt) {
            auto rd = readHIDTimeout(device_handle, response, hsc_device_timeout);
            if (!rd) {
                if (rd.error().code == DeviceError::Code::Timeout)
                    continue;
                return rd.error();
            }

            if (response[1] == BATTERY_TYPE && response[2] == BATTERY_SUBTYPE) {
                uint8_t right = response[BYTE_RIGHT_EARBUD];
                uint8_t left  = response[BYTE_LEFT_EARBUD];

                return BatteryResult {
                    .level_percent = std::min(left, right),
                    .status        = BATTERY_AVAILABLE,
                    .raw_data
                    = std::vector<uint8_t>(response.begin(), response.end()),
                };
            }
        }

        return DeviceError::timeout("No battery report received within attempt limit");
    }
};

} // namespace headsetcontrol
