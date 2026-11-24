#pragma once

#include "../../result_types.hpp"
#include "../device_utils.hpp"
#include "../hid_device.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <format>
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
 * Frames are 64 bytes on HID report ID 0x05 in the vendor collection on usage page 0xFF00
 * (usage 0x05 or 0x01 depending on the product, see GNP_USAGES), zero padded:
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
 * JabraGNPBase holds the protocol; the subclass says where the headset is with
 * headsetAddress() and which collection to open with getCapabilityDetail().
 * JabraGNPDevice fixes both for a named product; JabraGeneric (jabra_generic.hpp), the
 * fallback for Jabra products no driver lists, works them out at runtime.
 */
class JabraGNPBase : public HIDDevice {
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

    static constexpr uint8_t ADDR_HOST       = 0x00;
    static constexpr uint8_t ADDR_DONGLE_HS  = 0x04;
    static constexpr uint8_t ADDR_USB_HS     = 0x08;
    static constexpr uint16_t GNP_USAGE_PAGE = 0xFF00;

    static constexpr uint16_t VENDOR_JABRA = 0x0B0E;

    /// Usages of the GNP collection (page 0xFF00), in order of preference
    static constexpr std::array<uint16_t, 2> GNP_USAGES { 0x05, 0x01 };

    static constexpr bool isGnpCollection(const hid_device_info& info)
    {
        return info.usage_page == GNP_USAGE_PAGE
            && std::find(GNP_USAGES.begin(), GNP_USAGES.end(), info.usage) != GNP_USAGES.end();
    }

    constexpr uint16_t getVendorId() const override
    {
        return VENDOR_JABRA;
    }

protected:
    /**
     * @brief GNP address the headset answers at on this handle (0x04 or 0x08).
     */
    virtual Result<uint8_t> headsetAddress(hid_device* device_handle) = 0;

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

    using Frame = std::array<uint8_t, MSG_SIZE>;

    /**
     * @brief Send one GET/SET to a GNP address and return its response, NACKs included.
     *
     * Unsolicited events and stale replies (sequence mismatch) are skipped. A read
     * timeout is returned as DeviceError::timeout(); a reply cut off before the end of
     * the payload its header declares is a protocol error.
     */
    Result<Frame> exchange(hid_device* device_handle, uint8_t address, uint8_t type, uint8_t cmd, uint8_t sub,
        std::span<const uint8_t> payload = {}, int timeout_ms = hsc_device_timeout)
    {
        const uint8_t seq = sequence_++;
        if (auto result = writeHID(device_handle, makeFrame(address, type, cmd, sub, seq, payload)); !result) {
            return result.error();
        }

        // One deadline for the whole request: events in between do not extend it. The
        // first read always happens, so a timeout of 0 still picks up a queued reply.
        // A negative timeout means blocking reads, as for readHIDTimeout(): no deadline.
        const bool blocking = timeout_ms < 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(blocking ? 0 : timeout_ms);
        for (bool first_read = true;; first_read = false) {
            int wait_ms = -1;
            if (!blocking) {
                const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now())
                                           .count();
                if (remaining <= 0 && !first_read) {
                    return DeviceError::timeout("No reply for the requested command");
                }
                wait_ms = static_cast<int>(std::max<long long>(remaining, 0));
            }
            Frame response {};
            auto read_result = readHIDTimeout(device_handle, response, wait_ms);
            if (!read_result) {
                return read_result.error();
            }
            // Only what was read counts: the rest of the buffer is zeros, not data
            if (*read_result < OFF_PAYLOAD || !isReplyTo(response, address, type, cmd, sub, seq)) {
                continue;
            }
            if (*read_result < OFF_PAYLOAD + payloadLength(response)) {
                return DeviceError::protocolError("Truncated reply from the Jabra device");
            }
            return response;
        }
    }

    /**
     * @brief Whether @p response answers the request: addressed to the host, same sequence
     * number, and either a NACK, or from the address asked an ACK to a SET or the requested
     * command echoed. Anything else (events, other requests) is skipped.
     *
     * A NACK is accepted from any address. The Link 390 (0x2E51) sends its NACKs from the
     * address asked (captured with the Evolve3 75 off: F4 from 0x04, F5 from 0x08); other
     * dongles may answer from their own address (0x01), and dropping that NACK would make
     * every request wait for the full timeout.
     */
    static constexpr bool isReplyTo(const Frame& response, uint8_t address, uint8_t type, uint8_t cmd, uint8_t sub,
        uint8_t seq)
    {
        if (response[0] != REPORT_ID || response[OFF_DST] != ADDR_HOST || response[OFF_SEQ] != seq
            || (response[OFF_LEN] >> 6) != TYPE_RESPONSE) {
            return false;
        }
        const uint8_t reply_cmd = response[OFF_CMD];
        if (reply_cmd == CMD_NACK) {
            return true;
        }
        return response[OFF_SRC] == address
            && ((type == TYPE_SET && reply_cmd == CMD_ACK) || (reply_cmd == cmd && response[OFF_SUB] == sub));
    }

private:
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

    /// Levels written; the Evolve2 and Evolve3 models seen here offer exactly these
    static constexpr std::array<int8_t, 6> SIDETONE_DB { -9, -6, -3, 0, 3, 6 };
    /// Levels a headset may report (Jabra's catalog also has -4, -2, +2 and +4 dB)
    static constexpr std::array<int8_t, 10> SIDETONE_DB_KNOWN { -9, -6, -4, -3, -2, 0, 2, 3, 4, 6 };
    static constexpr uint8_t SIDETONE_MUTED   = 0x01;
    static constexpr uint8_t SIDETONE_UNMUTED = 0x00;

    struct SidetoneState {
        bool muted;
        int8_t db; // as reported, kept when only the mute flag changes
        uint8_t index; // nearest step of SIDETONE_DB
    };

    static constexpr uint8_t nearestSidetoneStep(int8_t db)
    {
        uint8_t best = 0;
        for (uint8_t i = 1; i < SIDETONE_DB.size(); ++i) {
            if (std::abs(SIDETONE_DB[i] - db) < std::abs(SIDETONE_DB[best] - db)) {
                best = i;
            }
        }
        return best;
    }

    // Shared by every Headset this driver instance serves, possibly from several threads
    std::atomic<uint8_t> sequence_ { 0 };

    static Frame makeFrame(uint8_t address, uint8_t type, uint8_t cmd, uint8_t sub, uint8_t seq, std::span<const uint8_t> payload)
    {
        Frame f {};
        f[0]       = REPORT_ID;
        f[OFF_DST] = address;
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
     * A NACK for "no device at that address" or "peer offline", or no reply at all,
     * is reported as device-offline.
     *
     * @return The reply frame (payload starts at OFF_PAYLOAD); for ACKs the payload is empty.
     */
    Result<Frame> request(hid_device* device_handle, uint8_t type, uint8_t cmd, uint8_t sub,
        std::span<const uint8_t> payload = {})
    {
        auto address = headsetAddress(device_handle);
        if (!address) {
            return address.error();
        }
        auto response = exchange(device_handle, *address, type, cmd, sub, payload);
        if (!response) {
            if (response.error().code == DeviceError::Code::Timeout) {
                return DeviceError::deviceOffline("No reply from the Jabra device");
            }
            return response.error();
        }
        if ((*response)[OFF_CMD] == CMD_NACK) {
            return nackError((*response)[OFF_SUB]);
        }
        return response;
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
        const auto db = static_cast<int8_t>((*reply)[OFF_PAYLOAD + 1]);
        if (std::find(SIDETONE_DB_KNOWN.begin(), SIDETONE_DB_KNOWN.end(), db) == SIDETONE_DB_KNOWN.end()) {
            return DeviceError::protocolError("Invalid Jabra sidetone value");
        }
        return SidetoneState {
            .muted = (*reply)[OFF_PAYLOAD] == SIDETONE_MUTED,
            .db    = db,
            .index = nearestSidetoneStep(db)
        };
    }

    Result<void> writeSidetone(hid_device* device_handle, const SidetoneState& state)
    {
        return set(device_handle, CMD_CONFIG, CONFIG_DSP_SIDETONE,
            { state.muted ? SIDETONE_MUTED : SIDETONE_UNMUTED, static_cast<uint8_t>(state.db) });
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
            .level_name    = state.muted ? std::string("muted") : (state.db == 0 ? std::string("0 dB") : std::format("{:+} dB", state.db))
        };
    }

public:
    constexpr int getCapabilities() const override
    {
        return B(CAP_SIDETONE) | B(CAP_SIDETONE_STATUS) | B(CAP_BATTERY_STATUS) | B(CAP_LIGHTS)
            | B(CAP_VOICE_PROMPTS) | B(CAP_INACTIVE_TIME) | B(CAP_VOLUME_LIMITER);
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
        // The first byte is the string length; the payload may be padded beyond it
        const size_t text_len = static_cast<uint8_t>(text.front());
        if (text_len == 0 || text_len > len - 1) {
            return DeviceError::protocolError("Invalid string reply");
        }
        text = text.substr(1, text_len);
        return std::string(text.substr(0, text.find('\0')));
    }

    /**
     * @brief Report the paired headset, not the dongle, as the product.
     *
     * A Link dongle pairs with any Jabra Bluetooth headset, so the model behind it is only
     * known at runtime: IDENT name/serial at headsetAddress(). A headset on USB reports its
     * own name the same way. If the headset is off the device's own HID strings are kept.
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
            state->db    = SIDETONE_DB[state->index];
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
        // Nearest 5-minute step. 0 is "never", so 1-2 minutes round up to 5 instead.
        auto units = static_cast<uint8_t>((minutes + INACTIVITY_STEP_MIN / 2) / INACTIVITY_STEP_MIN);
        if (minutes > 0 && units == 0) {
            units = 1;
        }
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

/**
 * @brief A named Jabra product whose headset address and GNP collection are known.
 *
 * @tparam HeadsetAddress GNP address of the headset: 0x04 behind a dongle, 0x08 on USB
 * @tparam GnpUsage Usage of the vendor collection (page 0xFF00) carrying report 5:
 *         0x05 on the Link 390, 0x01 on the Evolve2 65 Flex
 */
template <uint8_t HeadsetAddress, uint16_t GnpUsage>
class JabraGNPDevice : public JabraGNPBase {
public:
    constexpr capability_detail getCapabilityDetail([[maybe_unused]] enum capabilities cap) const override
    {
        return { .usagepage = GNP_USAGE_PAGE, .usageid = GnpUsage, .interface_id = 0x03 };
    }

protected:
    Result<uint8_t> headsetAddress([[maybe_unused]] hid_device* device_handle) override
    {
        return HeadsetAddress;
    }
};

}
