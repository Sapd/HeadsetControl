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
            | B(CAP_ANC) | B(CAP_ANC_STARTUP_MODE) | B(CAP_ANC_BUTTON_MODES)
            | B(CAP_MICROPHONE_ATTACHMENT_STATUS) | B(CAP_MICROPHONE_MUTE_STATUS);
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

    Result<AncResult> setAnc(hid_device* device_handle, uint8_t mode) override
    {
        return setSonyAnc(device_handle, mode);
    }

    Result<AncStartupModeResult> setAncStartupMode(hid_device* device_handle, uint8_t mode) override
    {
        return setSonyAncStartupMode(device_handle, mode);
    }

    Result<AncButtonModesResult> setAncButtonModes(
        hid_device* device_handle, const AncButtonModes& modes) override
    {
        return setSonyAncButtonModes(device_handle, modes);
    }

    Result<MicAttachmentStatusResult> getMicAttachmentStatus(hid_device* device_handle) override
    {
        return getSonyMicAttachmentStatus(device_handle);
    }

    Result<MicMuteStatusResult> getMicMuteStatus(hid_device* device_handle) override
    {
        return getSonyMicMuteStatus(device_handle);
    }

    // No CAP_MICROPHONE_VOLUME: H9 II microphone volume is the Windows capture endpoint volume
    // (AudioEndpointVolume.MasterVolumeLevelScalar), not a HID command. Sony EID 0x24 reports
    // headset mic mute state in payload byte 0, so it is exposed as CAP_MICROPHONE_MUTE_STATUS.

    // Auto Gain Control uses the Sony APO mic-side DRC pipeline (writing a mic YAML via apoCommunication.MakeMicYamlFile())
};

} // namespace headsetcontrol
