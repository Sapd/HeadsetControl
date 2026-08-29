#pragma once

#include "protocols/sony_inzone_protocol.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Sony INZONE H5 (WH-G500) wireless gaming headset
 *
 * Communicates via a 2.4 GHz USB dongle (VID 0x054C, PID 0x0EBF).
 * The control protocol is Sony vendor HCI-over-HID on usage page 0xFF04,
 * report ID 0x02.
 */
class SonyINZONEH5 : public protocols::SonyINZONEProtocol {
public:
    static constexpr std::array<uint16_t, 1> PRODUCT_IDS { 0x0EBF };

    std::vector<uint16_t> getProductIds() const override
    {
        return { PRODUCT_IDS.begin(), PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override { return "Sony INZONE H5"sv; }

    constexpr int getCapabilities() const override
    {
        return B(CAP_BATTERY_STATUS) | B(CAP_CHATMIX_STATUS)
            | B(CAP_SIDETONE) | B(CAP_MICROPHONE_VOLUME);
    }

    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        return getSonyBattery(device_handle);
    }

    Result<ChatmixResult> getChatmix(hid_device* device_handle) override
    {
        return getSonyChatmix(device_handle);
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        return setSonySidetone(device_handle, level);
    }

    Result<MicVolumeResult> setMicVolume(hid_device* device_handle, uint8_t volume) override
    {
        return setSonyMicVolume(device_handle, volume);
    }
};

} // namespace headsetcontrol
