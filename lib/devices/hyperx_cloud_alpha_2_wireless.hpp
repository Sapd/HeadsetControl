#pragma once

#include "../result_types.hpp"
#include "device_utils.hpp"
#include "hid_device.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <initializer_list>
#include <string_view>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief HyperX Cloud Alpha 2 Wireless Gaming Headset
 *
 * Features:
 * - Battery status (percentage, charging, voltage)
 * - Sidetone (mic monitoring) on/off and level
 * - Chat-mix (game/chat balance) status
 * - Equalizer preset (the three presets stored on the base station)
 *
 * The base station enumerates as two USB devices: 0x0abe (audio) and 0x08be (control MCU).
 * Commands go to the control MCU's vendor interface (interface 2, usage page 0xff13 / usage 0xff00)
 * as 64-byte reports laid out as [command][sub-command][payload...]. Replies use command + 1:
 *   52 01 -> 53 01 <connected>
 *   50 02 -> 51 02 <level %> <charge state> <temp °C, i16 LE> <voltage mV, u16 LE> ...
 *   60 02 -> 61 02 <2 bytes> <audio settings block>
 *   60 01 <audio settings block> writes the whole block back
 * In the audio settings block, byte 4 is sidetone on/off, byte 5 the sidetone level (0-20),
 * byte 8 the game/chat balance (0x7b all game .. 0x80 balanced .. 0x85 all chat) and byte 11 the
 * active equalizer preset (1-3); the other bytes are kept as read. NGENUITY writes the whole
 * block from its own cached state whenever one of these settings changes in its UI, so while it
 * runs it can undo changes made here.
 * The MCU also acknowledges every command (FF 01, command echoed at offset 14), rejects with
 * FF 02, and pushes unsolicited notifications (FB xx) - e.g. FB 0A 01/00 when the headset
 * links/unlinks. NGENUITY polls the same interface continuously, so replies have to be picked
 * out of unrelated traffic.
 *
 * Hardware verification was on Windows only (battery on battery power, charging, and with the
 * headset powered off; sidetone, chat-mix and equalizer preset); all platforms stay declared.
 *
 * Vendor: HP, Inc (0x03f0)
 */
class HyperXCloudAlpha2Wireless : public HIDDevice {
public:
    static constexpr uint16_t VENDOR_HP = 0x03f0;
    static constexpr std::array<uint16_t, 1> SUPPORTED_PRODUCT_IDS {
        0x08be // Cloud Alpha 2 Wireless base station (control MCU)
    };

    static constexpr int TIMEOUT_MS  = 1000;
    static constexpr size_t MSG_SIZE = 64;

    static constexpr uint8_t CMD_BATTERY     = 0x50;
    static constexpr uint8_t SUB_BATTERY     = 0x02;
    static constexpr uint8_t CMD_CONNECTION  = 0x52;
    static constexpr uint8_t SUB_CONNECTION  = 0x01;
    static constexpr uint8_t CMD_AUDIO       = 0x60;
    static constexpr uint8_t SUB_AUDIO_SET   = 0x01;
    static constexpr uint8_t SUB_AUDIO_GET   = 0x02;
    static constexpr uint8_t REPORT_RESPONSE = 0xff;
    static constexpr uint8_t RESPONSE_ACK    = 0x01;
    static constexpr uint8_t RESPONSE_NACK   = 0x02;
    static constexpr uint8_t NOT_A_REPLY     = 0x00;

    static constexpr uint8_t CHARGE_STATE_CHARGING = 0x01;

    // Audio settings block: offset in the 61 02 reply, offset in the 60 01 request, and length
    static constexpr size_t AUDIO_BLOCK_REPLY_OFFSET   = 4;
    static constexpr size_t AUDIO_BLOCK_REQUEST_OFFSET = 2;
    static constexpr size_t AUDIO_BLOCK_SIZE           = 12;
    static constexpr size_t SIDETONE_ENABLED           = 4;
    static constexpr size_t SIDETONE_LEVEL             = 5;
    static constexpr uint8_t SIDETONE_DEVICE_MAX       = 20;
    static constexpr size_t CHATMIX                    = 8;
    static constexpr uint8_t CHATMIX_BALANCED          = 0x80;
    static constexpr int CHATMIX_STEPS                 = 5; // per side of balanced
    static constexpr size_t EQUALIZER_PRESET           = 11;
    static constexpr uint8_t EQUALIZER_PRESETS         = 3;

    constexpr uint16_t getVendorId() const override
    {
        return VENDOR_HP;
    }

    std::vector<uint16_t> getProductIds() const override
    {
        return { SUPPORTED_PRODUCT_IDS.begin(), SUPPORTED_PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "HyperX Cloud Alpha 2 Wireless"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_BATTERY_STATUS) | B(CAP_SIDETONE) | B(CAP_SIDETONE_STATUS) | B(CAP_CHATMIX_STATUS)
            | B(CAP_EQUALIZER_PRESET);
    }

    uint8_t getEqualizerPresetsCount() const override
    {
        return EQUALIZER_PRESETS;
    }

    constexpr capability_detail getCapabilityDetail([[maybe_unused]] enum capabilities cap) const override
    {
        return { .usagepage = 0xff13, .usageid = 0xff00, .interface_id = 2 };
    }

    // Rich Results V2 API
    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        auto connection = query(device_handle, { CMD_CONNECTION, SUB_CONNECTION }, CMD_CONNECTION + 1);
        if (!connection) {
            return connection.error();
        }
        if ((*connection)[2] == 0x00) {
            return DeviceError::deviceOffline("Headset not connected");
        }

        auto battery = query(device_handle, { CMD_BATTERY, SUB_BATTERY }, CMD_BATTERY + 1);
        if (!battery) {
            return battery.error();
        }

        const auto& r     = *battery;
        const int level   = r[2];
        const uint8_t chg = r[3];
        if (level > 100) {
            return DeviceError::protocolError("Battery level out of range");
        }

        return BatteryResult {
            .level_percent = level,
            .status        = chg == CHARGE_STATE_CHARGING ? BATTERY_CHARGING : BATTERY_AVAILABLE,
            .voltage_mv    = r[6] | (r[7] << 8),
            .raw_data      = std::vector<uint8_t> { r.begin(), r.begin() + 12 }
        };
    }

    Result<SidetoneResult> getSidetone(hid_device* device_handle) override
    {
        auto block = readAudioSettings(device_handle);
        if (!block) {
            return block.error();
        }

        const bool enabled      = (*block)[SIDETONE_ENABLED] != 0;
        const uint8_t dev_level = (*block)[SIDETONE_LEVEL];
        return sidetoneResult(enabled ? map<uint8_t>(dev_level, 0, SIDETONE_DEVICE_MAX, 0, 128) : 0, enabled, dev_level);
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        // The base station only takes the whole audio settings block, so read it first and
        // change nothing but the sidetone bytes.
        auto block = readAudioSettings(device_handle);
        if (!block) {
            return block.error();
        }

        const bool enabled         = level > 0;
        auto settings              = *block;
        settings[SIDETONE_ENABLED] = enabled ? 0x01 : 0x00;
        if (enabled) {
            settings[SIDETONE_LEVEL] = std::max<uint8_t>(1, map<uint8_t>(level, 0, 128, 0, SIDETONE_DEVICE_MAX));
        }

        if (auto written = writeAudioSettings(device_handle, settings); !written) {
            return written.error();
        }

        return sidetoneResult(level, enabled, settings[SIDETONE_LEVEL]);
    }

    Result<ChatmixResult> getChatmix(hid_device* device_handle) override
    {
        auto block = readAudioSettings(device_handle);
        if (!block) {
            return block.error();
        }

        // -5 (all game) .. 0 (balanced) .. +5 (all chat)
        const int step = (*block)[CHATMIX] - CHATMIX_BALANCED;
        if (step < -CHATMIX_STEPS || step > CHATMIX_STEPS) {
            return DeviceError::protocolError("Chat-mix value out of range");
        }

        return ChatmixResult {
            .level               = 64 + step * 64 / CHATMIX_STEPS,
            .game_volume_percent = step > 0 ? 100 - step * 100 / CHATMIX_STEPS : 100,
            .chat_volume_percent = step < 0 ? 100 + step * 100 / CHATMIX_STEPS : 100
        };
    }

    Result<EqualizerPresetResult> setEqualizerPreset(hid_device* device_handle, uint8_t preset) override
    {
        if (preset >= EQUALIZER_PRESETS) {
            return DeviceError::invalidParameter("Device only supports presets 0-2");
        }

        auto block = readAudioSettings(device_handle);
        if (!block) {
            return block.error();
        }

        auto settings              = *block;
        settings[EQUALIZER_PRESET] = preset + 1; // the device counts presets from 1
        if (auto written = writeAudioSettings(device_handle, settings); !written) {
            return written.error();
        }

        return EqualizerPresetResult { .preset = preset, .total_presets = EQUALIZER_PRESETS };
    }

private:
    static SidetoneResult sidetoneResult(uint8_t level, bool enabled, uint8_t dev_level)
    {
        return SidetoneResult {
            .current_level = level,
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = 0,
            .device_max    = SIDETONE_DEVICE_MAX,
            .is_muted      = !enabled,
            .device_level  = dev_level
        };
    }

    Result<std::array<uint8_t, AUDIO_BLOCK_SIZE>> readAudioSettings(hid_device* device_handle) const
    {
        auto reply = query(device_handle, { CMD_AUDIO, SUB_AUDIO_GET }, CMD_AUDIO + 1);
        if (!reply) {
            return reply.error();
        }

        std::array<uint8_t, AUDIO_BLOCK_SIZE> block {};
        std::copy_n(reply->begin() + AUDIO_BLOCK_REPLY_OFFSET, AUDIO_BLOCK_SIZE, block.begin());
        if (block[SIDETONE_ENABLED] > 1 || block[SIDETONE_LEVEL] > SIDETONE_DEVICE_MAX
            || block[EQUALIZER_PRESET] < 1 || block[EQUALIZER_PRESET] > EQUALIZER_PRESETS) {
            return DeviceError::protocolError("Unexpected audio settings");
        }
        return block;
    }

    Result<std::array<uint8_t, MSG_SIZE>> writeAudioSettings(hid_device* device_handle,
        const std::array<uint8_t, AUDIO_BLOCK_SIZE>& settings) const
    {
        std::array<uint8_t, MSG_SIZE> request {};
        request[0] = CMD_AUDIO;
        request[1] = SUB_AUDIO_SET;
        std::copy(settings.begin(), settings.end(), request.begin() + AUDIO_BLOCK_REQUEST_OFFSET);
        return query(device_handle, request, NOT_A_REPLY);
    }

    Result<std::array<uint8_t, MSG_SIZE>> query(hid_device* device_handle,
        std::initializer_list<uint8_t> command, uint8_t reply_cmd) const
    {
        std::array<uint8_t, MSG_SIZE> request {};
        std::copy(command.begin(), command.end(), request.begin());
        return query(device_handle, request, reply_cmd);
    }

    /**
     * Send a request and wait for its [reply_cmd][sub] reply, or - with reply_cmd NOT_A_REPLY -
     * for the base station's acknowledgement. Skips acknowledgements, notifications and replies
     * to other software's requests.
     */
    Result<std::array<uint8_t, MSG_SIZE>> query(hid_device* device_handle,
        const std::array<uint8_t, MSG_SIZE>& request, uint8_t reply_cmd) const
    {
        const uint8_t cmd = request[0];
        const uint8_t sub = request[1];
        if (auto result = writeHID(device_handle, request); !result) {
            return result.error();
        }

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(TIMEOUT_MS);
        std::array<uint8_t, MSG_SIZE> response {};
        while (true) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now())
                                  .count();
            if (left <= 0) {
                return DeviceError::timeout("No reply from base station");
            }

            response.fill(0);
            auto read = readHIDTimeout(device_handle, response, static_cast<int>(left));
            if (!read) {
                return read.error();
            }

            const bool about_request = response[0] == REPORT_RESPONSE && response[14] == cmd && response[15] == sub;
            if (about_request && response[1] == RESPONSE_NACK) {
                return DeviceError::protocolError("Base station rejected the request");
            }
            if (reply_cmd == NOT_A_REPLY ? about_request && response[1] == RESPONSE_ACK
                                         : response[0] == reply_cmd && response[1] == sub) {
                return response;
            }
        }
    }
};
} // namespace headsetcontrol
