#pragma once

#include "../../result_types.hpp"
#include "../device_utils.hpp"
#include "../hid_device.hpp"

#include <array>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <vector>

namespace headsetcontrol::protocols {

class SonyINZONEProtocol : public HIDDevice {
protected:
    static constexpr uint16_t VENDOR_SONY = 0x054C;

    static constexpr int REPORT_SIZE   = 64;
    static constexpr uint8_t REPORT_ID = 0x02;

    static constexpr uint8_t HCI_TYPE_COMMAND = 0x01;
    static constexpr uint8_t HCI_TYPE_EVENT   = 0x04;
    static constexpr uint8_t SONY_EVENT_CODE  = 0xFF;
    static constexpr uint8_t SONY_OPCODE_LO   = 0x00;
    static constexpr uint8_t SONY_OPCODE_HI   = 0xFC;
    static constexpr uint8_t SONY_KEY_ID_LO   = 0x96;
    static constexpr uint8_t SONY_KEY_ID_HI   = 0xC3;

    static constexpr uint8_t ADDR_PC       = 0x1;
    static constexpr uint8_t ADDR_TX       = 0x2;
    static constexpr uint8_t ADDR_RX       = 0x4;
    static constexpr uint8_t ADDR_PC_TO_RX = (ADDR_RX << 4) | ADDR_PC;
    static constexpr uint8_t ADDR_PC_TO_TX = (ADDR_TX << 4) | ADDR_PC;

    static constexpr uint8_t ETYPE_GET         = 0x01;
    static constexpr uint8_t ETYPE_SET         = 0x02;
    static constexpr uint8_t ETYPE_RET         = 0x10;
    static constexpr uint8_t ETYPE_NTFY        = 0x20;
    static constexpr uint8_t ETYPE_NTFY_ACTIVE = 0xA0;

    static constexpr uint8_t EID_2GHZ_CONNECT_STATUS    = 0x01;
    static constexpr uint8_t EID_BATTERY_INFO           = 0x04;
    static constexpr uint8_t EID_HEADPHONE_VOLUME       = 0x21;
    static constexpr uint8_t EID_GAME_CHAT_MIX_BALANCE  = 0x22;
    static constexpr uint8_t EID_SIDETONE_VOLUME        = 0x23;
    static constexpr uint8_t EID_MIC_VOLUME             = 0x24;
    static constexpr uint8_t EID_AMB_SETTING             = 0x41;
    static constexpr uint8_t EID_NC_TOGGLE_SETTING       = 0x42;
    static constexpr uint8_t EID_NC_STARTUP_MODE         = 0x43;
    static constexpr uint8_t EID_AUTO_POWER_OFF_SETTING  = 0x81;
    static constexpr uint8_t EID_BT_STARTUP_MODE         = 0x63;
    static constexpr uint8_t EID_GUIDANCE_SETTING        = 0x84;
    static constexpr uint8_t EID_MIC_ATTACHED_STATUS     = 0x8F;

    static constexpr uint8_t DEVICE_VOLUME_MAX   = 50;
    static constexpr uint8_t DEVICE_BALANCE_MAX  = 90;
    static constexpr uint8_t DEVICE_SIDETONE_MAX = 50;
    static constexpr uint8_t DEVICE_MIC_VOL_MAX  = 50;

    static constexpr int READ_TIMEOUT_MS   = 500;
    static constexpr int MAX_READ_ATTEMPTS = 10;

    struct ParsedEvent {
        uint8_t event_id        = 0;
        uint8_t event_type      = 0;
        uint8_t address         = 0;
        uint16_t transaction_id = 0;
        std::vector<uint8_t> payload;
    };

    constexpr uint16_t getVendorId() const override { return VENDOR_SONY; }

    constexpr capability_detail getCapabilityDetail([[maybe_unused]] enum capabilities cap) const override
    {
        return { .usagepage = 0xFF04, .usageid = 0x0002, .interface_id = 5 };
    }

    Result<BatteryResult> getSonyBattery(hid_device* device_handle)
    {
        auto resp = exchange(device_handle, ADDR_PC_TO_RX, EID_BATTERY_INFO, ETYPE_GET, {});
        if (!resp) {
            return resp.error();
        }
        const auto& payload = resp->payload;
        if (payload.size() < 2) {
            return DeviceError::protocolError("BATTERY_INFO payload too short");
        }

        const uint8_t charger = payload[0];
        const uint8_t percent = payload[1];
        if (percent == 0xFF) {
            return DeviceError::deviceOffline("Headset reports battery=0xFF (offline)");
        }
        if (percent > 100) {
            return DeviceError::protocolError(
                std::format("Invalid battery percent: {}", percent));
        }

        return BatteryResult {
            .level_percent = percent,
            .status        = (charger != 0) ? BATTERY_CHARGING : BATTERY_AVAILABLE,
            .raw_data      = payload,
        };
    }

    Result<ChatmixResult> getSonyChatmix(hid_device* device_handle)
    {
        auto resp = exchange(device_handle, ADDR_PC_TO_RX, EID_GAME_CHAT_MIX_BALANCE, ETYPE_GET, {});
        if (!resp) {
            return resp.error();
        }
        const auto& payload = resp->payload;
        if (payload.empty()) {
            return DeviceError::protocolError("GAME_CHAT_MIX_BALANCE payload empty");
        }

        const uint8_t balance = payload[0];
        if (balance == 0xFF) {
            return DeviceError::deviceOffline("Headset offline");
        }
        if (balance > DEVICE_BALANCE_MAX) {
            return DeviceError::protocolError(
                std::format("Invalid balance: {}", balance));
        }

        const int chat_pct = (balance * 100) / DEVICE_BALANCE_MAX;
        const int game_pct = 100 - chat_pct;
        const int level    = map<int>(balance, 0, DEVICE_BALANCE_MAX, 0, 128);

        return ChatmixResult {
            .level               = level,
            .game_volume_percent = game_pct,
            .chat_volume_percent = chat_pct,
        };
    }

    Result<SidetoneResult> setSonySidetone(hid_device* device_handle, uint8_t level)
    {
        const uint8_t dev_level = map<uint8_t>(level, 0, 128, 0, DEVICE_SIDETONE_MAX);
        const std::array<uint8_t, 2> payload { dev_level, 0xFF };

        auto resp = exchange(device_handle, ADDR_PC_TO_RX, EID_SIDETONE_VOLUME, ETYPE_SET,
            std::span<const uint8_t> { payload });
        if (!resp) {
            return resp.error();
        }

        return SidetoneResult {
            .current_level = level,
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = 0,
            .device_max    = DEVICE_SIDETONE_MAX,
        };
    }

    Result<MicVolumeResult> setSonyMicVolume(hid_device* device_handle, uint8_t volume)
    {
        const uint8_t dev_level = map<uint8_t>(volume, 0, 128, 0, DEVICE_MIC_VOL_MAX);
        const std::array<uint8_t, 3> payload { 0x00, dev_level, 0xFF };

        auto resp = exchange(device_handle, ADDR_PC_TO_RX, EID_MIC_VOLUME, ETYPE_SET,
            std::span<const uint8_t> { payload });
        if (!resp) {
            return resp.error();
        }

        return MicVolumeResult {
            .volume     = volume,
            .min_volume = 0,
            .max_volume = 128,
        };
    }

    Result<InactiveTimeResult> setSonyInactiveTime(hid_device* device_handle, uint8_t minutes, bool h9ii_format)
    {
        if (minutes != 0 && minutes != 5 && minutes != 15 && minutes != 30 && minutes != 60 && minutes != 180) {
            return DeviceError::invalidParameter("Sony INZONE auto power off supports 0, 5, 15, 30, 60, or 180 minutes");
        }

        const std::array<uint8_t, 2> payload { minutes, minutes };
        auto resp = exchange(device_handle, ADDR_PC_TO_RX, EID_AUTO_POWER_OFF_SETTING, ETYPE_SET,
            std::span<const uint8_t> { payload }.first(h9ii_format ? 2 : 1));
        if (!resp) {
            return resp.error();
        }

        return InactiveTimeResult {
            .minutes     = minutes,
            .min_minutes = 0,
            .max_minutes = 180,
        };
    }

    Result<VoicePromptsResult> setSonyVoicePrompts(hid_device* device_handle, bool enabled)
    {
        const std::array<uint8_t, 1> payload { static_cast<uint8_t>(enabled ? 1 : 0) };
        auto resp = exchange(device_handle, ADDR_PC_TO_RX, EID_GUIDANCE_SETTING, ETYPE_SET,
            std::span<const uint8_t> { payload });
        if (!resp) {
            return resp.error();
        }

        return VoicePromptsResult { .enabled = enabled };
    }

    Result<MicAttachedResult> getSonyMicAttached(hid_device* device_handle)
    {
        auto resp = exchange(device_handle, ADDR_PC_TO_RX, EID_MIC_ATTACHED_STATUS, ETYPE_GET, {});
        if (!resp) {
            return resp.error();
        }
        if (resp->payload.empty()) {
            return DeviceError::protocolError("MIC_ATTACHED_STATUS payload empty");
        }
        return MicAttachedResult { .attached = (resp->payload[0] == 0) };
    }

    Result<MicMuteStatusResult> getSonyMicMuteStatus(hid_device* device_handle)
    {
        auto resp = exchange(device_handle, ADDR_PC_TO_RX, EID_MIC_VOLUME, ETYPE_GET, {});
        if (!resp) {
            return resp.error();
        }
        if (resp->payload.empty()) {
            return DeviceError::protocolError("MIC_VOLUME payload empty");
        }
        if (resp->payload[0] != 0 && resp->payload[0] != 1) {
            return DeviceError::protocolError(
                std::format("Invalid mic mute status: {}", resp->payload[0]));
        }
        return MicMuteStatusResult { .muted = (resp->payload[0] == 1) };
    }

    Result<AncStartupModeResult> setSonyANCStartupMode(hid_device* device_handle, uint8_t mode)
    {
        if (mode > 3) {
            return DeviceError::invalidParameter("ANC startup mode must be 0 (off), 1 (NC), 2 (ambient), or 3 (mode at power off)");
        }
        const std::array<uint8_t, 1> payload { mode };
        auto resp = exchange(device_handle, ADDR_PC_TO_RX, EID_NC_STARTUP_MODE, ETYPE_SET,
            std::span<const uint8_t> { payload });
        if (!resp) {
            return resp.error();
        }
        return AncStartupModeResult { .mode = mode };
    }

    Result<AncResult> setSonyANC(hid_device* device_handle, uint8_t mode)
    {
        if (mode > 2) {
            return DeviceError::invalidParameter("ANC mode must be 0 (off), 1 (ANC), or 2 (ambient sound)");
        }
        // Payload: [nc_setting, ambient_volume_value, ambient_volume_percent, voice_focus]
        const std::array<uint8_t, 4> payload { mode, 20, 0xFF, 0 };
        auto resp = exchange(device_handle, ADDR_PC_TO_RX, EID_AMB_SETTING, ETYPE_SET,
            std::span<const uint8_t> { payload });
        if (!resp) {
            return resp.error();
        }
        return AncResult { .mode = mode };
    }

    Result<AncToggleModesResult> setSonyANCToggleModes(
        hid_device* device_handle, bool off_enabled, bool anc_enabled, bool ambient_enabled)
    {
        if (!off_enabled && !anc_enabled && !ambient_enabled) {
            return DeviceError::invalidParameter("At least one ANC toggle mode must be enabled");
        }

        const std::array<uint8_t, 3> payload {
            static_cast<uint8_t>(off_enabled ? 1 : 0),
            static_cast<uint8_t>(anc_enabled ? 1 : 0),
            static_cast<uint8_t>(ambient_enabled ? 1 : 0),
        };
        auto resp = exchange(device_handle, ADDR_PC_TO_RX, EID_NC_TOGGLE_SETTING, ETYPE_SET,
            std::span<const uint8_t> { payload });
        if (!resp) {
            return resp.error();
        }
        return AncToggleModesResult {
            .off_enabled     = off_enabled,
            .anc_enabled     = anc_enabled,
            .ambient_enabled = ambient_enabled,
        };
    }

    Result<BluetoothWhenPoweredOnResult> setSonyBluetoothWhenPoweredOn(hid_device* device_handle, bool enabled)
    {
        const std::array<uint8_t, 1> payload { static_cast<uint8_t>(enabled ? 1 : 0) };
        auto resp = exchange(device_handle, ADDR_PC_TO_RX, EID_BT_STARTUP_MODE, ETYPE_SET,
            std::span<const uint8_t> { payload });
        if (!resp) {
            return resp.error();
        }

        return BluetoothWhenPoweredOnResult { .enabled = enabled };
    }

    Result<ParsedEvent> exchange(hid_device* device_handle, uint8_t address,
        uint8_t event_id, uint8_t event_type, std::span<const uint8_t> payload)
    {
        uint16_t tid = ++transaction_counter_;
        if (tid <= 1) {
            tid = transaction_counter_ = 2;
        }

        std::array<uint8_t, REPORT_SIZE> buf {};
        buildCommand(buf, address, event_id, event_type, tid, payload);

        if (auto wr = writeHID(device_handle, buf); !wr) {
            return wr.error();
        }

        const uint8_t want_type = (event_type == ETYPE_SET) ? ETYPE_NTFY : ETYPE_RET;

        for (int attempt = 0; attempt < MAX_READ_ATTEMPTS; ++attempt) {
            std::array<uint8_t, REPORT_SIZE> resp {};
            auto rd = readHIDTimeout(device_handle, resp, READ_TIMEOUT_MS);
            if (!rd) {
                if (rd.error().code == DeviceError::Code::Timeout) {
                    continue;
                }
                return rd.error();
            }

            auto parsed = parseEvent(resp);
            if (!parsed) {
                continue;
            }

            if (parsed->event_id == event_id
                && parsed->transaction_id == tid
                && (parsed->event_type == want_type
                    || parsed->event_type == ETYPE_NTFY_ACTIVE)) {
                return *parsed;
            }
        }

        return DeviceError::timeout(
            std::format("No response for event_id 0x{:02x} (TID {})", event_id, tid));
    }

    static void buildCommand(std::array<uint8_t, REPORT_SIZE>& buf,
        uint8_t address, uint8_t event_id, uint8_t event_type,
        uint16_t tid, std::span<const uint8_t> payload)
    {
        const size_t payload_len = payload.size();
        const size_t hid_length  = 12 + payload_len;

        buf[0] = REPORT_ID;
        buf[1] = static_cast<uint8_t>(hid_length);

        buf[2]  = HCI_TYPE_COMMAND;
        buf[3]  = SONY_OPCODE_LO;
        buf[4]  = SONY_OPCODE_HI;
        buf[5]  = static_cast<uint8_t>(8 + payload_len);
        buf[6]  = SONY_KEY_ID_LO;
        buf[7]  = SONY_KEY_ID_HI;
        buf[8]  = address;
        buf[9]  = event_id;
        buf[10] = event_type;
        buf[11] = static_cast<uint8_t>(tid & 0xFF);
        buf[12] = static_cast<uint8_t>((tid >> 8) & 0xFF);

        for (size_t i = 0; i < payload_len; ++i) {
            buf[13 + i] = payload[i];
        }

        unsigned sum = 0;
        for (size_t i = 6; i <= 12 + payload_len; ++i) {
            sum += buf[i];
        }
        buf[13 + payload_len] = static_cast<uint8_t>(sum & 0xFF);
    }

    static std::optional<ParsedEvent> parseEvent(const std::array<uint8_t, REPORT_SIZE>& buf)
    {
        if (buf[0] != REPORT_ID) {
            return std::nullopt;
        }
        const uint8_t hid_length = buf[1];
        if (hid_length < 12 || hid_length > REPORT_SIZE - 2) {
            return std::nullopt;
        }

        if (buf[2] != HCI_TYPE_EVENT)
            return std::nullopt;
        if (buf[3] != SONY_EVENT_CODE)
            return std::nullopt;
        if (buf[5] != 0x00)
            return std::nullopt;
        if (buf[6] != SONY_KEY_ID_LO || buf[7] != SONY_KEY_ID_HI)
            return std::nullopt;

        const uint8_t address = buf[8];
        if ((address >> 4) != ADDR_PC)
            return std::nullopt;

        unsigned sum = 0;
        for (size_t i = 5; i <= hid_length; ++i) {
            sum += buf[i];
        }
        if (static_cast<uint8_t>(sum & 0xFF) != buf[hid_length + 1]) {
            return std::nullopt;
        }

        return ParsedEvent {
            .event_id       = buf[9],
            .event_type     = buf[10],
            .address        = address,
            .transaction_id = static_cast<uint16_t>(buf[11] | (buf[12] << 8)),
            .payload        = (hid_length > 12)
                       ? std::vector<uint8_t>(buf.begin() + 13, buf.begin() + hid_length + 1)
                       : std::vector<uint8_t> {},
        };
    }

private:
    uint16_t transaction_counter_ = 0;
};

} // namespace headsetcontrol::protocols
