#pragma once

#include "../result_types.hpp"
#include "hid_device.hpp"
#include <array>
#include <chrono>
#include <string_view>
#include <thread>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief HyperX Cloud Alpha S (wired, USB audio control box)
 *
 * Features:
 * - Sidetone (on/off only)
 * - Sidetone status
 * - Chat-mix status (game/chat balance buttons on the control box)
 * - Microphone mute status
 *
 * The control box exposes a vendor collection (usage page 0xff13) on interface 5.
 * Every command is a 62 byte output report with report ID 0x06; the reply is fetched
 * with a GET_REPORT for the same ID. Byte 10 selects the parameter, bytes 7-9 are a
 * fixed module id. The sidetone command toggles (like the 7.1 button does), so it is
 * only sent when the status reply shows a different state.
 */
class HyperXCloudAlphaS : public HIDDevice {
public:
    static constexpr uint16_t VENDOR_KINGSTON = 0x0951;
    static constexpr std::array<uint16_t, 1> SUPPORTED_PRODUCT_IDS { 0x16ed };

    static constexpr int MSG_SIZE           = 62;
    static constexpr uint8_t REPORT_ID      = 0x06;
    static constexpr uint8_t PARAM_STATUS   = 0x09;
    static constexpr uint8_t PARAM_SIDETONE = 0x07;
    static constexpr int REPLY_DELAY_MS     = 100;

    // Status reply layout
    static constexpr int STATUS_FLAGS   = 14; // bit 0: sidetone on, bit 1: microphone muted
    static constexpr int STATUS_GAME_DB = 16; // Q8.8 big-endian, 0 dB = full volume
    static constexpr int STATUS_CHAT_DB = 20;
    static constexpr int BALANCE_STEPS  = 8;

    uint16_t getVendorId() const override
    {
        return VENDOR_KINGSTON;
    }

    std::vector<uint16_t> getProductIds() const override
    {
        return { SUPPORTED_PRODUCT_IDS.begin(), SUPPORTED_PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "HyperX Cloud Alpha S"sv;
    }

    int getCapabilities() const override
    {
        return B(CAP_SIDETONE) | B(CAP_CHATMIX_STATUS) | B(CAP_MICROPHONE_MUTE_STATUS);
    }

    int getReadableCapabilities() const override
    {
        return B(CAP_SIDETONE);
    }

    constexpr capability_detail getCapabilityDetail([[maybe_unused]] enum capabilities cap) const override
    {
        return { .usagepage = 0xff13, .usageid = 0x0001, .interface_id = 5 };
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        const bool on = level > 0;
        auto current  = getSidetone(device_handle);
        if (!current) {
            return current.error();
        }
        if (current->is_muted == on) {
            if (auto result = command(device_handle, 0x01, PARAM_SIDETONE); !result) {
                return result.error();
            }
        }
        return sidetoneResult(on);
    }

    Result<SidetoneResult> getSidetone(hid_device* device_handle) override
    {
        auto status = command(device_handle, 0x01, PARAM_STATUS);
        if (!status) {
            return status.error();
        }
        return sidetoneResult(((*status)[STATUS_FLAGS] & 0x01) != 0);
    }

    Result<MicMuteStatusResult> getMicMuteStatus(hid_device* device_handle) override
    {
        auto status = command(device_handle, 0x01, PARAM_STATUS);
        if (!status) {
            return status.error();
        }
        return MicMuteStatusResult { .muted = ((*status)[STATUS_FLAGS] & 0x02) != 0 };
    }

    Result<ChatmixResult> getChatmix(hid_device* device_handle) override
    {
        auto status = command(device_handle, 0x01, PARAM_STATUS);
        if (!status) {
            return status.error();
        }

        // Each side is attenuated in steps of 0 (full) .. -8 (silent)
        const int game  = attenuationStep(readQ8_8(*status, STATUS_GAME_DB));
        const int chat  = attenuationStep(readQ8_8(*status, STATUS_CHAT_DB));
        const int level = 64 - (game - chat) * 64 / BALANCE_STEPS;

        return ChatmixResult {
            .level               = level,
            .game_volume_percent = 100 + game * 100 / BALANCE_STEPS,
            .chat_volume_percent = 100 + chat * 100 / BALANCE_STEPS
        };
    }

    // Maps the attenuation in dB to a balance step, using the same thresholds as NGENUITY
    static constexpr int attenuationStep(float db)
    {
        constexpr std::array<std::pair<float, int>, 8> THRESHOLDS { {
            { -40.0f, -8 },
            { -30.0f, -7 },
            { -20.0f, -6 },
            { -14.0f, -5 },
            { -10.0f, -4 },
            { -6.0f, -3 },
            { -3.0f, -2 },
            { -1.0f, -1 },
        } };
        for (const auto& [limit, step] : THRESHOLDS) {
            if (db < limit) {
                return step;
            }
        }
        return 0;
    }

private:
    static float readQ8_8(const std::array<uint8_t, MSG_SIZE>& data, int offset)
    {
        const auto raw = static_cast<int16_t>((data[offset] << 8) | data[offset + 1]);
        return static_cast<float>(raw) / 256.0f;
    }

    static SidetoneResult sidetoneResult(bool on)
    {
        return SidetoneResult {
            .current_level = static_cast<uint8_t>(on ? 128 : 0),
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = 0,
            .device_max    = 1,
            .is_muted      = !on,
            .device_level  = static_cast<uint8_t>(on ? 1 : 0)
        };
    }

    Result<std::array<uint8_t, MSG_SIZE>> command(hid_device* device_handle, uint8_t kind, uint8_t param, uint8_t value = 0)
    {
        std::array<uint8_t, MSG_SIZE> cmd {};
        cmd[0]  = REPORT_ID;
        cmd[2]  = kind;
        cmd[4]  = 0x9b;
        cmd[5]  = value;
        cmd[7]  = 0x68;
        cmd[8]  = 0x4a;
        cmd[9]  = 0x8e;
        cmd[10] = param;

        if (auto result = writeHID(device_handle, cmd); !result) {
            return result.error();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(REPLY_DELAY_MS));

        std::array<uint8_t, MSG_SIZE> reply {};
        reply[0]  = REPORT_ID;
        auto read = getInputReport(device_handle, reply);
        if (!read) {
            return read.error();
        }
        // The device echoes the request header and sets bit 7 of byte 5 as an acknowledgement
        if (*read < STATUS_CHAT_DB + 2 || reply[0] != REPORT_ID || reply[4] != 0x9b || (reply[5] & 0x80) == 0) {
            return DeviceError::protocolError("Unexpected reply from HyperX Cloud Alpha S");
        }
        return reply;
    }
};
} // namespace headsetcontrol
