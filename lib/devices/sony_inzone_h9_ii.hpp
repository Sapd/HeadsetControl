#pragma once

#include "protocols/sony_inzone_protocol.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Sony INZONE H9 II wireless gaming headset
 *
 * Communicates via a 2.4 GHz USB dongle (VID 0x054C, PID 0x0FA8).
 * INZONE Hub identifies the PC HID control collection as MI_05&COL03,
 * with the same Sony vendor HCI-over-HID protocol as INZONE H5.
 */
class SonyINZONEH9II : public protocols::SonyINZONEProtocol {
public:
    static constexpr std::array<uint16_t, 1> PRODUCT_IDS { 0x0FA8 };

    std::vector<uint16_t> getProductIds() const override
    {
        return { PRODUCT_IDS.begin(), PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override { return "Sony INZONE H9 II"sv; }

    constexpr int getCapabilities() const override
    {
        return B(CAP_BATTERY_STATUS) | B(CAP_CHATMIX_STATUS)
            | B(CAP_SIDETONE) | B(CAP_INACTIVE_TIME)
            | B(CAP_VOICE_PROMPTS) | B(CAP_BT_WHEN_POWERED_ON)
            | B(CAP_ANC) | B(CAP_ANC_STARTUP_MODE) | B(CAP_MICROPHONE_ATTACHMENT_STATUS)
            | B(CAP_MICROPHONE_MUTE_STATUS) | B(CAP_ANC_TOGGLE_MODES);
    }

    constexpr capability_detail getCapabilityDetail([[maybe_unused]] enum capabilities cap) const override
    {
        return { .usagepage = 0xFF04, .usageid = 0x0001, .interface_id = 5 };
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

    Result<InactiveTimeResult> setInactiveTime(hid_device* device_handle, uint8_t minutes) override
    {
        return setSonyInactiveTime(device_handle, minutes, true);
    }

    Result<VoicePromptsResult> setVoicePrompts(hid_device* device_handle, bool enabled) override
    {
        return setSonyVoicePrompts(device_handle, enabled);
    }

    Result<BluetoothWhenPoweredOnResult> setBluetoothWhenPoweredOn(hid_device* device_handle, bool enabled) override
    {
        return setSonyBluetoothWhenPoweredOn(device_handle, enabled);
    }

    Result<AncResult> setANC(hid_device* device_handle, uint8_t mode) override
    {
        return setSonyANC(device_handle, mode);
    }

    Result<AncStartupModeResult> setANCStartupMode(hid_device* device_handle, uint8_t mode) override
    {
        return setSonyANCStartupMode(device_handle, mode);
    }

    Result<AncToggleModesResult> setANCToggleModes(
        hid_device* device_handle, bool off_enabled, bool anc_enabled, bool ambient_enabled) override
    {
        return setSonyANCToggleModes(device_handle, off_enabled, anc_enabled, ambient_enabled);
    }

    Result<MicAttachedResult> getMicAttached(hid_device* device_handle) override
    {
        return getSonyMicAttached(device_handle);
    }

    Result<MicMuteStatusResult> getMicMuteStatus(hid_device* device_handle) override
    {
        return getSonyMicMuteStatus(device_handle);
    }

    // H9 II microphone volume is the Windows capture endpoint volume (AudioEndpointVolume.MasterVolumeLevelScalar).
    // Sony EID 0x24 reports headset mic mute state in payload byte 0, so it is exposed as CAP_MICROPHONE_MUTE_STATUS.
    Result<MicVolumeResult> setMicVolume([[maybe_unused]] hid_device* device_handle, [[maybe_unused]] uint8_t volume) override
    {
        return DeviceError::notSupported("H9 II mic volume is a Windows audio endpoint setting, not a HID command");
    }

    // Auto Gain Control uses the Sony APO mic-side DRC pipeline (writing a mic YAML via apoCommunication.MakeMicYamlFile())
};

} // namespace headsetcontrol
