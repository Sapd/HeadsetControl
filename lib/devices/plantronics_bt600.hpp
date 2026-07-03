#pragma once

#include "../result_types.hpp"
#include "device_utils.hpp"
#include "hid_device.hpp"
#include <array>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Plantronics/Poly Voyager 8200 UC (via BT600 USB dongle)
 *
 * Features:
 * - Sidetone (3 discrete levels: Low / Medium / High)
 * - Battery status (charging state + remaining talk time, reported as an
 *   estimated percentage plus time-to-empty minutes)
 * - Lights (the "online indicator" LED)
 * - Voice prompts (the "notification tones" setting)
 * - Volume limiter (the "noise exposure" limit, mapped to 85 dB on/off)
 *
 * Protocol
 * --------
 * The dongle speaks Poly's native get/set protocol tunneled over HID report
 * ID 0x07 in the vendor collection usage-page 0xFFA2 / usage 0x03 on
 * interface 3. Commands are sent as OUTPUT reports (SET_REPORT via EP0; the
 * interface has no interrupt-OUT endpoint) and answered as INPUT reports on
 * interrupt EP 0x81. Frame layout (64-byte report, zero padded):
 *
 *   07 01 01 10 <len> <addr> 00 00 <op> <grp> <item> <value...>
 *
 * The addr byte selects the target: 0x00 = the dongle itself, 0x20 = the
 * headset via Bluetooth. Replies from the headset carry addr 0x02, replies
 * from the dongle carry addr 0x00 (same as its unsolicited events).
 *
 *   HELLO (open session):  op=0x01  ->  07 01 01 10 07 <addr> 00 00 01 01 02
 *   HELLO ack:             op=0x08  ->  07 01 01 10 06 <addr> 00 00 08 01 02
 *   GET (read a setting):  op=0x02  ->  07 01 01 10 06 20 00 00 02 GG II
 *   SET (write a setting): op=0x05  ->  07 01 01 10 07 20 00 00 05 GG II VV
 *   GET reply:             op=0x03  ->  07 01 01 10 LL 02 00 00 03 GG II <value>
 *   SET ack ("changed"):   op=0x0a  ->  07 01 01 10 LL 02 00 00 0a GG II VV
 *
 * The dongle ignores every GET/SET until sessions have been opened, which
 * requires (in this order, as the Windows stack does at device attach): a
 * 2-byte output report 06 01, a HELLO addressed to the dongle (arms the
 * relay to the headset; without it a freshly powered-on headset never
 * answers), and a HELLO addressed to the headset. Sessions persist until
 * the dongle loses power or the headset reboots, and the sequence is
 * idempotent, so it is sent before every operation. Each HELLO ack is
 * followed by a multi-fragment catalogue of supported settings (frames
 * whose header bytes differ from 01 01 10); those are skipped by the reply
 * matcher, as are unsolicited events.
 *
 * With the headset powered off or out of range the headset-addressed HELLO
 * and all GETs/SETs stay unanswered, so a read timeout is reported as
 * device-offline.
 *
 * Battery (group 0x0A, item 0x1A) value: <level> <level_count> <charging>
 * <minutes:u16be> <marker=01>, e.g. 07 0B 00 02 EE 01.
 *   level       = current charge level (0 .. level_count-1)
 *   level_count = number of discrete levels including 0 (0x0B = 11 here, so
 *                 the denominator is 10)
 *   charging    = 0/1
 *   minutes     = remaining talk time (time to empty), reported separately
 * Percentage = level / (level_count - 1) * 100. This is Poly's own formula:
 * its software computes level/numLevels*100 (numLevels = level_count-1), which
 * was verified live (level 7 -> 70%, level 6 -> 60%, matching Poly Studio).
 * The talk-time minutes are reported as time-to-empty; they drift within a
 * level band, which is why they are not used for the percentage.
 *
 * Known settings without a matching HeadsetControl capability (names are
 * Poly's own, from the Deckard device model in Poly Studio; documented here
 * so the knowledge is not lost):
 *
 *   grp/item  Poly name           values
 *   04/03     volumeLevelTone     atEveryLevel / minMaxOnly
 *   04/06     secondInboundCall   ignore / once / continuous
 *   04/08     scoTone             off / on   (active audio tone)
 *   04/0A     muteAlert           off / timed / voiceAudible / voiceVisible
 *   04/0B     muteTone            voice / singleTone / doubleTone
 *   04/0C     answeringCallVP     off / on   (answering-call voice prompt)
 *   04/0D     incomingCallVibration  off / on
 *   04/0E     chargerPluginVibration off / on
 *   04/0F     ancTimerDuration    off / 2 / 4
 *   08/04     callerID            off / on   (00 / FF)
 *   0A/0E     A2DP                off / on   (streaming audio)
 *   0A/22     muteReminderFrequency  1..15
 *   0A/30     spokenAnswerIgnore  off / on
 *   0F/0C     G616                off / on   (anti-startle / sound limiting)
 *   0F/0E     twa                 off / 85db / 80db   (implemented, see below)
 *   0F/10     twaPeriod           2 / 4 / 6 / 8 (hours)
 *   0F/15     ringToneMobile      sound1 / sound2 / sound3 / off
 *   0F/16     ringToneVoip        sound1 / sound2 / sound3 / off
 *   0F/18     audioBandwidthMobile  narrowband / wideband   (HD voice)
 *   0F/3A     extendedRangeMode   off / on
 *
 * (Plus reporting toggles: acousticIncidentReporting 0F/01, aalTwaReporting
 * 0F/07, conversationDynamicsReporting 0F/0D, linkQualityReporting 0F/54.)
 *
 * Read-only items observed on the headset address: 0A/00 product name
 * (UTF-16), 0A/01 serial number string, 0A/03 build code string, 0A/1E
 * Bluetooth address. Open mic (listen-through) is not a settable item; its
 * state arrives as a "changed" event on 0E/1E (0x0C=on, 0x00=off), and each
 * headset button press fires an event on 0E/2B. The 0x9A report is a
 * separate feature-report request/response channel for device info
 * (SET_FEATURE 9a 60 00 01 <n> then GET_FEATURE: n=01 dongle firmware,
 * n=02 headset firmware, n=04 friendly name, n=06 headset Bluetooth ID,
 * n=07 dongle serial) and for the event queue (9b 01 input pulse signals a
 * pending event; SET_FEATURE 9a 40 00 then GET_FEATURE pops it).
 *
 * Reverse-engineered from USBPcap/usbmon captures of Plantronics Hub and
 * Poly Studio and validated live against the hardware (firmware v2120).
 */
class PlantronicsBT600 : public HIDDevice {
public:
    static constexpr size_t MSG_SIZE   = 64;
    static constexpr uint8_t REPORT_ID = 0x07;

    static constexpr std::array<uint16_t, 1> PRODUCT_IDS {
        0x02EE // Plantronics BT600 dongle (Voyager 8200 UC)
    };

    // Field offsets inside a reply frame
    static constexpr size_t OFF_ADDR  = 5; // source/target address
    static constexpr size_t OFF_OP    = 8;
    static constexpr size_t OFF_GRP   = 9;
    static constexpr size_t OFF_ITEM  = 10;
    static constexpr size_t OFF_VALUE = 11; // first value byte

    // Battery reply value layout:
    //   <level> <level_count> <charging> <minutes hi> <minutes lo> <marker>
    static constexpr size_t OFF_BATT_LEVEL    = 11;
    static constexpr size_t OFF_BATT_COUNT    = 12; // number of levels incl. 0
    static constexpr size_t OFF_BATT_CHARGING = 13;
    static constexpr size_t OFF_BATT_MIN_HI   = 14;
    static constexpr size_t OFF_BATT_MIN_LO   = 15;

private:
    // Fixed frame header/markers
    static constexpr uint8_t HDR1 = 0x01, HDR2 = 0x01, HDR3 = 0x10;
    static constexpr uint8_t ADDR_HEADSET       = 0x20; // request to the headset
    static constexpr uint8_t ADDR_HEADSET_REPLY = 0x02; // reply from the headset
    static constexpr uint8_t ADDR_DONGLE        = 0x00; // request to / reply from the dongle
    static constexpr uint8_t OP_HELLO           = 0x01;
    static constexpr uint8_t OP_GET             = 0x02;
    static constexpr uint8_t OP_SET             = 0x05;
    static constexpr uint8_t OP_HELLO_ACK       = 0x08;
    static constexpr uint8_t OP_GET_REPLY       = 0x03;

    // Output report that precedes session setup in the Windows attach
    // sequence; required before a freshly powered-on headset will answer.
    static constexpr std::array<uint8_t, 2> SESSION_NUDGE { 0x06, 0x01 };

    // HELLO command payload (sits in the grp/item slots of the frame)
    static constexpr uint8_t HELLO_ARG1 = 0x01;
    static constexpr uint8_t HELLO_ARG2 = 0x02;

    // Setting addresses (group / item)
    static constexpr uint8_t GRP_SETTINGS     = 0x04;
    static constexpr uint8_t ITEM_ONLINE_LED  = 0x09; // Poly "enableOLI"
    static constexpr uint8_t ITEM_SIDETONE    = 0x10; // Poly "sideToneLevel"
    static constexpr uint8_t ITEM_NOTIF_TONES = 0x11; // Poly "enableNotificationTones"
    static constexpr uint8_t GRP_BATTERY      = 0x0A;
    static constexpr uint8_t ITEM_BATTERY     = 0x1A;
    static constexpr uint8_t GRP_CALL_AUDIO   = 0x0F;
    static constexpr uint8_t ITEM_TWA_LIMIT   = 0x0E; // Poly "twa" (noise dose limit)

    // twa (time-weighted-average noise limit): 00=off, 01=85 dB, 02=80 dB
    static constexpr uint8_t TWA_OFF  = 0x00;
    static constexpr uint8_t TWA_85DB = 0x01;

    // Sidetone device levels
    static constexpr uint8_t SIDETONE_LEVELS = 3; // 0=Low, 1=Medium, 2=High

    // Skip this many non-matching reports (events, catalogue fragments)
    // before giving up on a reply
    static constexpr int MAX_READ_ATTEMPTS = 16;

    /**
     * @brief Build a request frame.
     */
    static constexpr std::array<uint8_t, MSG_SIZE> makeFrame(uint8_t op, uint8_t grp, uint8_t item, uint8_t value, uint8_t addr = ADDR_HEADSET)
    {
        std::array<uint8_t, MSG_SIZE> f {};
        f[0]  = REPORT_ID;
        f[1]  = HDR1;
        f[2]  = HDR2;
        f[3]  = HDR3;
        f[4]  = (op == OP_GET) ? uint8_t(0x06) : uint8_t(0x07); // payload length
        f[5]  = addr;
        f[8]  = op;
        f[9]  = grp;
        f[10] = item;
        if (op == OP_SET)
            f[11] = value;
        return f;
    }

    /**
     * @brief Read until a solicited reply for (grp, item) arrives.
     *
     * Skips unsolicited events (dir=0x00), catalogue fragments (header
     * bytes differ) and replies for other settings. A read timeout means
     * the headset is not linked to the dongle.
     *
     * @param op Expected reply op, or 0 to accept any solicited reply
     */
    Result<void> awaitReply(hid_device* device_handle, uint8_t op, uint8_t grp, uint8_t item, std::array<uint8_t, MSG_SIZE>& response, uint8_t addr = ADDR_HEADSET_REPLY) const
    {
        for (int attempt = 0; attempt < MAX_READ_ATTEMPTS; ++attempt) {
            auto read_result = readHIDTimeout(device_handle, response, hsc_device_timeout);
            if (!read_result) {
                if (read_result.error().code == DeviceError::Code::Timeout) {
                    return DeviceError::deviceOffline("Headset not connected to BT600 adapter (powered off or out of range)");
                }
                return read_result.error();
            }
            if (isReplyFor(response, grp, item, addr) && (op == 0 || response[OFF_OP] == op)) {
                return {};
            }
        }
        return DeviceError::protocolError("No reply for the requested setting");
    }

    /**
     * @brief Open the dongle- and headset-side settings sessions.
     *
     * Replays the init the Windows stack performs at device attach: the
     * 06 01 output report, a HELLO to the dongle (arms the relay to the
     * headset), and a HELLO to the headset. Required after the dongle
     * powers up or the headset reboots; idempotent, so it is sent before
     * every operation.
     */
    Result<void> openSession(hid_device* device_handle) const
    {
        if (auto result = writeHID(device_handle, SESSION_NUDGE); !result) {
            return result.error();
        }

        auto dongle_hello = makeFrame(OP_HELLO, HELLO_ARG1, HELLO_ARG2, 0, ADDR_DONGLE);
        if (auto result = writeHID(device_handle, dongle_hello); !result) {
            return result.error();
        }

        std::array<uint8_t, MSG_SIZE> response {};
        if (auto result = awaitReply(device_handle, OP_HELLO_ACK, HELLO_ARG1, HELLO_ARG2, response, ADDR_DONGLE); !result) {
            return result.error();
        }

        auto headset_hello = makeFrame(OP_HELLO, HELLO_ARG1, HELLO_ARG2, 0, ADDR_HEADSET);
        if (auto result = writeHID(device_handle, headset_hello); !result) {
            return result.error();
        }

        return awaitReply(device_handle, OP_HELLO_ACK, HELLO_ARG1, HELLO_ARG2, response, ADDR_HEADSET_REPLY);
    }

    /**
     * @brief Open the sessions, write a setting and wait for the device ack.
     */
    Result<void> applySetting(hid_device* device_handle, uint8_t grp, uint8_t item, uint8_t value)
    {
        if (auto result = openSession(device_handle); !result) {
            return result.error();
        }

        auto request = makeFrame(OP_SET, grp, item, value);
        if (auto result = writeHID(device_handle, request); !result) {
            return result.error();
        }

        // The device acks a SET with a "changed" report for the same
        // group/item; without it the write went nowhere (headset off).
        std::array<uint8_t, MSG_SIZE> response {};
        return awaitReply(device_handle, 0, grp, item, response);
    }

public:
    constexpr uint16_t getVendorId() const override
    {
        return 0x047F; // Plantronics, Inc.
    }

    constexpr std::vector<uint16_t> getProductIds() const override
    {
        return { PRODUCT_IDS.begin(), PRODUCT_IDS.end() };
    }

    constexpr std::string_view getDeviceName() const override
    {
        return "Plantronics Voyager 8200 UC (BT600)"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_LIGHTS) | B(CAP_VOICE_PROMPTS) | B(CAP_VOLUME_LIMITER);
    }

    constexpr uint8_t getSupportedPlatforms() const override
    {
        // Untested on macOS: the dongle exposes five top-level HID
        // collections and macOS opens only the first enumerated one, which
        // may not be the Poly control collection (see get_hid_path).
        return PLATFORM_LINUX | PLATFORM_WINDOWS;
    }

    constexpr capability_detail getCapabilityDetail([[maybe_unused]] enum capabilities cap) const override
    {
        // Poly vendor control collection on interface 3.
        return { .usagepage = 0xFFA2, .usageid = 0x03, .interface_id = 0x03 };
    }

    /**
     * @brief Whether a report is a solicited reply for the given setting.
     *
     * The header check also rejects the catalogue fragments the dongle
     * streams after a HELLO (their bytes 1-3 differ from 01 01 10).
     *
     * @param addr Expected source address (0x02 = headset, 0x00 = dongle)
     */
    static constexpr bool isReplyFor(const std::array<uint8_t, MSG_SIZE>& response, uint8_t grp, uint8_t item, uint8_t addr = 0x02)
    {
        return response[0] == REPORT_ID && response[1] == HDR1
            && response[2] == HDR2 && response[3] == HDR3
            && response[OFF_ADDR] == addr
            && response[OFF_GRP] == grp && response[OFF_ITEM] == item;
    }

    /**
     * @brief Parse the value of a battery GET reply (group 0x0A, item 0x1A).
     *
     * Percentage is level / (level_count - 1) * 100, exactly as Poly's own
     * software computes it. The remaining talk time is reported separately as
     * time-to-empty.
     */
    static Result<BatteryResult> parseBatteryReply(const std::array<uint8_t, MSG_SIZE>& response)
    {
        const uint8_t level = response[OFF_BATT_LEVEL];
        const uint8_t count = response[OFF_BATT_COUNT];
        if (count < 2) {
            // Need at least two levels for a denominator; anything less is not
            // a battery structure (e.g. an empty/short reply).
            return DeviceError::protocolError("Unexpected battery reply format");
        }

        const bool charging = response[OFF_BATT_CHARGING] != 0;
        const int minutes   = bytes_to_uint16_be(response[OFF_BATT_MIN_HI], response[OFF_BATT_MIN_LO]);
        const int percent   = std::min(100, level * 100 / (count - 1));

        BatteryResult battery {};
        battery.level_percent     = percent;
        battery.status            = charging ? BATTERY_CHARGING : BATTERY_AVAILABLE;
        battery.time_to_empty_min = minutes;
        battery.raw_data          = std::vector<uint8_t>(response.begin(), response.end());
        return battery;
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        // Map normalized 0-128 onto the 3 device levels (0=Low, 1=Medium, 2=High).
        const uint8_t device_level = mapSidetoneToDiscrete<SIDETONE_LEVELS>(level);

        if (auto result = applySetting(device_handle, GRP_SETTINGS, ITEM_SIDETONE, device_level); !result) {
            return result.error();
        }

        return SidetoneResult {
            .current_level = device_level,
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = 0,
            .device_max    = SIDETONE_LEVELS - 1
        };
    }

    Result<LightsResult> setLights(hid_device* device_handle, bool on) override
    {
        if (auto result = applySetting(device_handle, GRP_SETTINGS, ITEM_ONLINE_LED, on ? 0x01 : 0x00); !result) {
            return result.error();
        }

        return LightsResult { .enabled = on };
    }

    Result<VoicePromptsResult> setVoicePrompts(hid_device* device_handle, bool enabled) override
    {
        if (auto result = applySetting(device_handle, GRP_SETTINGS, ITEM_NOTIF_TONES, enabled ? 0x01 : 0x00); !result) {
            return result.error();
        }

        return VoicePromptsResult { .enabled = enabled };
    }

    Result<VolumeLimiterResult> setVolumeLimiter(hid_device* device_handle, bool enabled) override
    {
        // Poly "twa" noise-dose limit; switching between "85 dB" and off. The
        // device also has an 80 dB option (0x02) not exposed by this boolean
        // capability.
        if (auto result = applySetting(device_handle, GRP_CALL_AUDIO, ITEM_TWA_LIMIT, enabled ? TWA_85DB : TWA_OFF); !result) {
            return result.error();
        }

        return VolumeLimiterResult { .enabled = enabled };
    }

    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        if (auto result = openSession(device_handle); !result) {
            return result.error();
        }

        auto request = makeFrame(OP_GET, GRP_BATTERY, ITEM_BATTERY, 0);
        if (auto result = writeHID(device_handle, request); !result) {
            return result.error();
        }

        std::array<uint8_t, MSG_SIZE> response {};
        if (auto result = awaitReply(device_handle, OP_GET_REPLY, GRP_BATTERY, ITEM_BATTERY, response); !result) {
            return result.error();
        }

        return parseBatteryReply(response);
    }
};

} // namespace headsetcontrol
