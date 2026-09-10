#pragma once

#include "../../result_types.hpp"
#include "../device_utils.hpp"
#include "../hid_device.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace headsetcontrol::protocols {

/**
 * @brief Jabra GN Protocol (GNP) over HID
 *
 * Common implementation for Jabra headsets and Link dongles. The protocol is the same
 * whether the headset is on a USB cable or reached through a Link dongle; only the GNP
 * address of the headset differs (0x08 for a headset on USB, 0x04 for the headset paired
 * to a dongle).
 *
 * Frames are 64 bytes on HID report ID 0x05 in the vendor collection usage page 0xFF00 /
 * usage 0x05, zero padded:
 *
 *   05 <dst> <src> <seq> <type<<6 | len> <cmd> <sub> <payload...>
 *
 *   dst/src : 0x00 host, 0x01 dongle, 0x04 headset via dongle, 0x08 headset on USB
 *   seq     : host counter, echoed in the reply
 *   type    : 1 GET, 2 SET, 3 RESPONSE, 0 unsolicited event; len counts bytes from
 *             dst to the end of the payload (6 = no payload)
 *   cmd/sub : command group / sub-command; replies use cmd 0xFF (ACK) or 0xFE (NACK,
 *             sub = reason: 0xF3 headset offline, 0xF5 no device at that address,
 *             0xF6 unknown group, 0xFF unknown sub-command)
 *
 * Commands used:
 *
 *   02/00 NAME, 02/01 SERIAL   GET -> [len][string]
 *   12/02 HS_BATTERY           GET -> [status bits][level %][adc hi][adc lo]...
 *                              status bit0 charging, bit1 low, bit6 fast charging
 *   12/08 BUSY_STATE           GET -> [state]; SET [state][00 = manual control]
 *   13/7C DSP_SIDETONE         GET/SET [mute 01=muted][level dB, signed: 06 03 00 FD FA F7]
 *   13/3A HS_VOICE_PROMPTS     GET/SET [00 tones, 01 voice, 02 off]
 *   13/90 INACTIVITY_INTERVAL  GET/SET [units of 5 minutes, 0 = never]
 *   13/01 INTELLITONE_LEVEL    GET/SET [00 PeakStop only, 03 IntelliTone, 05 G616]
 *
 * @tparam HeadsetAddress GNP address of the headset: 0x04 behind a dongle, 0x08 on USB
 * @tparam GnpUsage Usage of the vendor collection (page 0xFF00) carrying report 5:
 *         0x05 on the Link 390, 0x01 on the Evolve2 65 Flex
 */
template <uint8_t HeadsetAddress, uint16_t GnpUsage>
class JabraGNPDevice : public HIDDevice {
public:
    static constexpr size_t MSG_SIZE   = 64;
    static constexpr uint8_t REPORT_ID = 0x05;

    static constexpr size_t OFF_DST     = 1;
    static constexpr size_t OFF_SRC     = 2;
    static constexpr size_t OFF_SEQ     = 3;
    static constexpr size_t OFF_LEN     = 4;
    static constexpr size_t OFF_CMD     = 5;
    static constexpr size_t OFF_SUB     = 6;
    static constexpr size_t OFF_PAYLOAD = 7;

private:
    static constexpr uint8_t ADDR_HOST = 0x00;

    static constexpr uint8_t TYPE_GET      = 0x01;
    static constexpr uint8_t TYPE_SET      = 0x02;
    static constexpr uint8_t TYPE_RESPONSE = 0x03;

    static constexpr uint8_t CMD_ACK  = 0xFF;
    static constexpr uint8_t CMD_NACK = 0xFE;

    static constexpr uint8_t NACK_PEER_OFFLINE = 0xF3;
    static constexpr uint8_t NACK_NO_CHILD     = 0xF4;
    static constexpr uint8_t NACK_NO_DEVICE    = 0xF5;
    static constexpr uint8_t NACK_NO_GROUP     = 0xF6;
    static constexpr uint8_t NACK_NO_SUBCMD    = 0xFF;

    static constexpr uint8_t CMD_IDENT    = 0x02;
    static constexpr uint8_t IDENT_NAME   = 0x00;
    static constexpr uint8_t IDENT_SERIAL = 0x01;

    static constexpr uint8_t CMD_STATUS        = 0x12;
    static constexpr uint8_t STATUS_HS_BATTERY = 0x02;
    static constexpr uint8_t STATUS_BUSY_STATE = 0x08;

    static constexpr uint8_t CMD_CONFIG           = 0x13;
    static constexpr uint8_t CONFIG_INTELLITONE   = 0x01;
    static constexpr uint8_t CONFIG_VOICE_PROMPTS = 0x3A;
    static constexpr uint8_t CONFIG_DSP_SIDETONE  = 0x7C;
    static constexpr uint8_t CONFIG_INACTIVITY    = 0x90;

    static constexpr uint8_t BATTERY_FLAG_CHARGING = 0x01;

    static constexpr uint8_t VOICE_PROMPTS_TONES = 0x00;
    static constexpr uint8_t VOICE_PROMPTS_VOICE = 0x01;

    static constexpr uint8_t INTELLITONE_PEAKSTOP = 0x00;
    static constexpr uint8_t INTELLITONE_ON       = 0x03;

    static constexpr uint8_t INACTIVITY_STEP_MIN = 5;

    static constexpr std::array<int8_t, 6> SIDETONE_DB { -9, -6, -3, 0, 3, 6 };
    static constexpr std::array<std::string_view, 6> SIDETONE_NAMES {
        "-9 dB"sv, "-6 dB"sv, "-3 dB"sv, "0 dB"sv, "+3 dB"sv, "+6 dB"sv
    };
    static constexpr uint8_t SIDETONE_MUTED   = 0x01;
    static constexpr uint8_t SIDETONE_UNMUTED = 0x00;

    struct SidetoneState {
        bool muted;
        uint8_t index;
    };

    static constexpr int MAX_READ_ATTEMPTS = 16;

    uint8_t sequence_ = 0;

    using Frame = std::array<uint8_t, MSG_SIZE>;

    static Frame makeFrame(uint8_t type, uint8_t cmd, uint8_t sub, uint8_t seq, std::span<const uint8_t> payload)
    {
        Frame f {};
        f[0]       = REPORT_ID;
        f[OFF_DST] = HeadsetAddress;
        f[OFF_SRC] = ADDR_HOST;
        f[OFF_SEQ] = seq;
        f[OFF_LEN] = static_cast<uint8_t>((type << 6) | (6 + payload.size()));
        f[OFF_CMD] = cmd;
        f[OFF_SUB] = sub;
        std::copy_n(payload.begin(), std::min(payload.size(), MSG_SIZE - OFF_PAYLOAD), f.begin() + OFF_PAYLOAD);
        return f;
    }

    static DeviceError nackError(uint8_t reason)
    {
        switch (reason) {
        case NACK_PEER_OFFLINE:
        case NACK_NO_CHILD:
        case NACK_NO_DEVICE:
            return DeviceError::deviceOffline("Headset not connected to the Jabra dongle (powered off or out of range)");
        case NACK_NO_GROUP:
        case NACK_NO_SUBCMD:
            return DeviceError::notSupported("The paired Jabra headset does not implement this setting");
        default:
            return DeviceError::protocolError("Jabra device rejected the command (NACK)");
        }
    }

    static constexpr size_t payloadLength(const Frame& f)
    {
        const size_t len = f[OFF_LEN] & 0x3F;
        return len > 6 ? len - 6 : 0;
    }

    /**
     * @brief Send one GET/SET to the headset and wait for its response.
     *
     * Unsolicited events and stale replies (sequence mismatch) are skipped. A NACK for
     * "no device at that address" or "peer offline" is reported as device-offline.
     *
     * @return The reply frame (payload starts at OFF_PAYLOAD); for ACKs the payload is empty.
     */
    Result<Frame> request(hid_device* device_handle, uint8_t type, uint8_t cmd, uint8_t sub,
        std::span<const uint8_t> payload = {})
    {
        const uint8_t seq = sequence_++;
        if (auto result = writeHID(device_handle, makeFrame(type, cmd, sub, seq, payload)); !result) {
            return result.error();
        }

        for (int attempt = 0; attempt < MAX_READ_ATTEMPTS; ++attempt) {
            Frame response {};
            auto read_result = readHIDTimeout(device_handle, response, hsc_device_timeout);
            if (!read_result) {
                if (read_result.error().code == DeviceError::Code::Timeout) {
                    return DeviceError::deviceOffline("No reply from the Jabra dongle");
                }
                return read_result.error();
            }
            if (response[0] != REPORT_ID || (response[OFF_LEN] >> 6) != TYPE_RESPONSE
                || response[OFF_SEQ] != seq) {
                continue;
            }
            if (response[OFF_CMD] == CMD_NACK) {
                return nackError(response[OFF_SUB]);
            }
            return response;
        }
        return DeviceError::protocolError("No reply for the requested command");
    }

    Result<Frame> get(hid_device* device_handle, uint8_t cmd, uint8_t sub, std::span<const uint8_t> payload = {})
    {
        return request(device_handle, TYPE_GET, cmd, sub, payload);
    }

    Result<void> set(hid_device* device_handle, uint8_t cmd, uint8_t sub, std::initializer_list<uint8_t> payload)
    {
        auto result = request(device_handle, TYPE_SET, cmd, sub, payload);
        if (!result) {
            return result.error();
        }
        if ((*result)[OFF_CMD] != CMD_ACK) {
            return DeviceError::protocolError("Jabra device did not acknowledge the setting");
        }
        return {};
    }

    /**
     * @brief Read the current sidetone setting (mute flag and gain step) from the headset.
     */
    Result<SidetoneState> readSidetone(hid_device* device_handle)
    {
        auto reply = get(device_handle, CMD_CONFIG, CONFIG_DSP_SIDETONE);
        if (!reply) {
            return reply.error();
        }
        if (payloadLength(*reply) < 2) {
            return DeviceError::protocolError("Unexpected Jabra sidetone reply format");
        }
        const auto db   = static_cast<int8_t>((*reply)[OFF_PAYLOAD + 1]);
        const auto step = std::find(SIDETONE_DB.begin(), SIDETONE_DB.end(), db);
        if (step == SIDETONE_DB.end()) {
            return DeviceError::protocolError("Invalid Jabra sidetone value");
        }
        return SidetoneState {
            .muted = (*reply)[OFF_PAYLOAD] == SIDETONE_MUTED,
            .index = static_cast<uint8_t>(step - SIDETONE_DB.begin())
        };
    }

    Result<void> writeSidetone(hid_device* device_handle, const SidetoneState& state)
    {
        return set(device_handle, CMD_CONFIG, CONFIG_DSP_SIDETONE,
            { state.muted ? SIDETONE_MUTED : SIDETONE_UNMUTED, static_cast<uint8_t>(SIDETONE_DB[state.index]) });
    }

    static SidetoneResult toSidetoneResult(const SidetoneState& state)
    {
        return SidetoneResult {
            .current_level = state.muted ? uint8_t { 0 } : mapDiscreteToSidetone<SIDETONE_DB.size()>(state.index),
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = 0,
            .device_max    = SIDETONE_DB.size() - 1,
            .is_muted      = state.muted,
            .device_level  = state.index,
            .level_name    = std::string(state.muted ? "muted"sv : SIDETONE_NAMES[state.index])
        };
    }

public:
    constexpr uint16_t getVendorId() const override
    {
        return 0x0B0E;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_SIDETONE) | B(CAP_SIDETONE_STATUS) | B(CAP_BATTERY_STATUS) | B(CAP_LIGHTS)
            | B(CAP_VOICE_PROMPTS) | B(CAP_INACTIVE_TIME) | B(CAP_VOLUME_LIMITER);
    }

    constexpr capability_detail getCapabilityDetail([[maybe_unused]] enum capabilities cap) const override
    {
        return { .usagepage = 0xFF00, .usageid = GnpUsage, .interface_id = 0x03 };
    }

    /**
     * @brief Read a length-prefixed string reply ("[len][chars]") from the headset.
     */
    Result<std::string> getString(hid_device* device_handle, uint8_t cmd, uint8_t sub)
    {
        auto reply = get(device_handle, cmd, sub);
        if (!reply) {
            return reply.error();
        }
        const size_t len = payloadLength(*reply);
        if (len == 0) {
            return DeviceError::protocolError("Empty string reply");
        }
        std::string_view text(reinterpret_cast<const char*>(reply->data() + OFF_PAYLOAD), len);
        if (static_cast<uint8_t>(text.front()) == len - 1) {
            text.remove_prefix(1);
        }
        return std::string(text.substr(0, text.find('\0')));
    }

    /**
     * @brief Report the paired headset, not the dongle, as the product.
     *
     * The Link 390 pairs with any Jabra Bluetooth headset, so the model behind the dongle
     * is only known at runtime: IDENT name/serial at address 0x04. If the headset is off
     * the dongle's own HID strings are kept.
     */
    Result<DeviceMetadata> getMetadata(hid_device* device_handle) override
    {
        auto meta = HIDDevice::getMetadata(device_handle);
        if (!meta) {
            return meta;
        }
        if (auto name = getString(device_handle, CMD_IDENT, IDENT_NAME); name && !name->empty()) {
            meta->product = *name;
            if (auto serial = getString(device_handle, CMD_IDENT, IDENT_SERIAL); serial && !serial->empty()) {
                meta->serial_number = *serial;
            }
        }
        return meta;
    }

    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        auto reply = get(device_handle, CMD_STATUS, STATUS_HS_BATTERY);
        if (!reply) {
            return reply.error();
        }
        if (payloadLength(*reply) < 2) {
            return DeviceError::protocolError("Unexpected battery reply format");
        }
        const uint8_t flags = (*reply)[OFF_PAYLOAD];
        const uint8_t level = (*reply)[OFF_PAYLOAD + 1];

        BatteryResult battery {};
        battery.level_percent = std::min<int>(100, level);
        battery.status        = (flags & BATTERY_FLAG_CHARGING) ? BATTERY_CHARGING : BATTERY_AVAILABLE;
        battery.raw_data      = std::vector<uint8_t>(reply->begin(), reply->begin() + static_cast<long>(OFF_PAYLOAD + payloadLength(*reply)));
        return battery;
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        auto state = readSidetone(device_handle);
        if (!state) {
            return state.error();
        }
        state->muted = level == 0;
        if (!state->muted) {
            state->index = mapSidetoneToDiscrete<SIDETONE_DB.size()>(level);
        }
        if (auto result = writeSidetone(device_handle, *state); !result) {
            return result.error();
        }
        return toSidetoneResult(*state);
    }

    Result<SidetoneResult> getSidetone(hid_device* device_handle) override
    {
        auto state = readSidetone(device_handle);
        if (!state) {
            return state.error();
        }
        return toSidetoneResult(*state);
    }

    Result<LightsResult> setLights(hid_device* device_handle, bool on) override
    {
        if (auto result = set(device_handle, CMD_STATUS, STATUS_BUSY_STATE, { static_cast<uint8_t>(on ? 1 : 0), 0x00 }); !result) {
            return result.error();
        }
        return LightsResult { .enabled = on };
    }

    Result<VoicePromptsResult> setVoicePrompts(hid_device* device_handle, bool enabled) override
    {
        if (auto result = set(device_handle, CMD_CONFIG, CONFIG_VOICE_PROMPTS, { enabled ? VOICE_PROMPTS_VOICE : VOICE_PROMPTS_TONES }); !result) {
            return result.error();
        }
        return VoicePromptsResult { .enabled = enabled };
    }

    Result<InactiveTimeResult> setInactiveTime(hid_device* device_handle, uint8_t minutes) override
    {
        const uint8_t units = static_cast<uint8_t>((minutes + INACTIVITY_STEP_MIN / 2) / INACTIVITY_STEP_MIN);
        if (auto result = set(device_handle, CMD_CONFIG, CONFIG_INACTIVITY, { units }); !result) {
            return result.error();
        }
        return InactiveTimeResult {
            .minutes     = static_cast<uint8_t>(units * INACTIVITY_STEP_MIN),
            .min_minutes = 0,
            .max_minutes = 255 / INACTIVITY_STEP_MIN * INACTIVITY_STEP_MIN
        };
    }

    Result<VolumeLimiterResult> setVolumeLimiter(hid_device* device_handle, bool enabled) override
    {
        if (auto result = set(device_handle, CMD_CONFIG, CONFIG_INTELLITONE, { enabled ? INTELLITONE_ON : INTELLITONE_PEAKSTOP }); !result) {
            return result.error();
        }
        return VolumeLimiterResult { .enabled = enabled };
    }
};

}
