#pragma once

#include "device_utils.hpp"
#include "hid_device.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <string_view>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Logitech ASTRO A50 Gen 5 base station — USB ID 046d:0b1c
 *
 * Reverse-engineered protocol (see ghub-reverse/NOTES.md). The base station
 * exposes a single vendor HID interface (interface 8, usage page 0xFF32) plus
 * eight USB Audio Class interfaces. Control rides report ID 0x02 on the vendor
 * interface as 64-byte interrupt reports.
 *
 * This is NOT HID++ and NOT the newer "Centurion" protocol (report 0x51, used by
 * the A50 X). It is a simple vendor command protocol:
 *
 *   byte[0] = 0x02   report ID
 *   byte[1] = 0x0c   constant marker
 *   byte[2] = LEN    number of meaningful bytes that follow (= 3 + payload_len)
 *   byte[3] = 0x00
 *   byte[4] = CMD    command / property id
 *   byte[5] = HANDLE transaction handle (request: low-nibble 0xC; push/reply: 0x00)
 *   byte[6..] = payload (little-endian)
 *
 * A GET is a request with no payload; the device replies with the same CMD and a
 * value payload. The device also pushes these frames unsolicited on state change.
 *
 * Features implemented:
 * - Battery status   (CMD 0x06) — reply byte6 = %, byte8 = dock/charging flag
 * - Chatmix          (CMD 0x0a) — value 0..12 (0 = chat/voice, 12 = game)
 * - Sidetone         (CMD 0x09) — payload 01 ff <level>
 * - Lights/dock LED  (CMD 0x0f) — payload <brightness 0..100>
 * - Parametric EQ    (CMD 0x0d) — 10 bands [freqBE16][Q LE16][gainB]; Q=scale/32,
 *                                 gain byte=120+dB*20 (±6 dB).
 * - Noise filter     (CMD 0x14) — mic noise gate; HSC 0/1/2 -> A50 Off/Night/Tournament.
 *
 * All six capabilities were verified on real hardware (A50 base 046d:0b1c): lights toggle
 * the dock LED, battery reads %/charging, chatmix reads 0..12 (linear), sidetone is audible,
 * the parametric EQ visibly shifts the sound (a ±6 dB bass boost/cut was clearly audible while
 * music played), and the noise gate is audible on the mic. The byte5 handle is an echo token
 * (the device echoes it back) — any value works, which is why the battery GET (handle 0x0c)
 * reads correctly even though G HUB only ever pushed it.
 */
class LogitechAstroA50 : public HIDDevice {
public:
    static constexpr std::array<uint16_t, 1> PRODUCT_IDS { 0x0b1c };

    // Frame structure
    static constexpr uint8_t REPORT_ID = 0x02;
    static constexpr uint8_t MARKER    = 0x0c;
    static constexpr size_t FRAME_SIZE = 64;
    static constexpr int POLL_ATTEMPTS = 8;

    // Command ids (byte[4])
    static constexpr uint8_t CMD_BATTERY    = 0x06;
    static constexpr uint8_t CMD_SIDETONE   = 0x09;
    static constexpr uint8_t CMD_CHATMIX    = 0x0a;
    static constexpr uint8_t CMD_EQ         = 0x0d;
    static constexpr uint8_t CMD_BRIGHTNESS = 0x0f;
    static constexpr uint8_t CMD_NOISE_GATE = 0x14;

    // Transaction handles (byte[5]) — observed per command in captures
    static constexpr uint8_t HANDLE_CHATMIX    = 0x0c;
    static constexpr uint8_t HANDLE_SIDETONE   = 0x1c;
    static constexpr uint8_t HANDLE_BRIGHTNESS = 0x1c;
    static constexpr uint8_t HANDLE_EQ         = 0x2c; // observed in eq-audio.pcapng
    static constexpr uint8_t HANDLE_NOISE_GATE = 0x2d; // observed in a50-noisegate.pcapng
    static constexpr uint8_t HANDLE_BATTERY    = 0x0c; // handle is an echo token (verified)

    // Noise gate (CMD 0x14) — device has 4 levels; HeadsetControl noise filter is 0/1/2.
    // Map off/low/high -> A50 Off(0x00) / Night(0x01) / Tournament(0x04).
    static constexpr std::array<uint8_t, 3> NOISE_LEVELS { 0x00, 0x01, 0x04 };

    // Parametric EQ (CMD 0x0d) — 10 fixed standard bands. Each band on the wire is
    // 5 bytes: [freq BE16][Q LE16][gain B]. Q = scale/32 (0x16 = 0.6875 default).
    // Gain byte: 0x78 = 0 dB, 240 = +6 dB, 0 = -6 dB → byte = 120 + dB*20 (20 units/dB).
    static constexpr int EQ_BANDS               = 10;
    static constexpr uint8_t EQ_GAIN_CENTER     = 120; // 0x78 = 0 dB
    static constexpr float EQ_GAIN_UNITS_PER_DB = 20.0f;
    static constexpr float EQ_GAIN_MIN_DB       = -6.0f;
    static constexpr float EQ_GAIN_MAX_DB       = 6.0f;
    static constexpr float EQ_Q_SCALE           = 32.0f; // Q = scale / 32
    static constexpr float EQ_Q_MIN             = 0.031f;
    static constexpr float EQ_Q_MAX             = 7.969f;
    static constexpr std::array<uint16_t, EQ_BANDS> EQ_FREQS {
        20, 50, 125, 250, 500, 1000, 2500, 5000, 10000, 20000
    };

    uint16_t getVendorId() const override { return VENDOR_LOGITECH; }

    std::vector<uint16_t> getProductIds() const override
    {
        return { PRODUCT_IDS.begin(), PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override { return "Logitech ASTRO A50 Gen 5"sv; }

    constexpr int getCapabilities() const override
    {
        return B(CAP_BATTERY_STATUS) | B(CAP_CHATMIX_STATUS) | B(CAP_SIDETONE) | B(CAP_LIGHTS)
            | B(CAP_PARAMETRIC_EQUALIZER) | B(CAP_NOISE_FILTER);
    }

    constexpr capability_detail getCapabilityDetail([[maybe_unused]] enum capabilities cap) const override
    {
        // Control lives on the vendor HID: interface 8 (Linux/macOS),
        // usage page 0xFF32 / usage 0x74 (Windows).
        return { .usagepage = 0xff32, .usageid = 0x0074, .interface_id = 8 };
    }

    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        // Reply (confirmed): 02 0c 06 00 06 00 <level%> <level2> <dock/charging>
        auto resp = sendRequest(device_handle, CMD_BATTERY, HANDLE_BATTERY, {}, /*read_reply=*/true);
        if (!resp) {
            return resp.error();
        }

        const auto& f       = *resp;
        const int level     = f[6]; // byte6 = battery percent (0..100)
        const bool dock_chg = f[8] != 0; // byte8 = 1 when docked/charging

        return BatteryResult {
            .level_percent = level,
            .status        = dock_chg ? BATTERY_CHARGING : BATTERY_AVAILABLE,
        };
    }

    Result<ChatmixResult> getChatmix(hid_device* device_handle) override
    {
        // GET (confirmed): 02 0c 03 00 0a 0c   reply: 02 0c 04 00 0a 0c <raw 0..12>
        auto resp = sendRequest(device_handle, CMD_CHATMIX, HANDLE_CHATMIX, {}, /*read_reply=*/true);
        if (!resp) {
            return resp.error();
        }

        // Device raw: 0 = full chat/voice, 6 = center, 12 = full game.
        // HeadsetControl level: 0..128 with <64 = game, >64 = chat.
        const int raw   = (*resp)[6];
        const int level = map<int>(raw, 0, 12, 128, 0);

        const int game_pct = (level <= 64) ? 100 : map<int>(level, 64, 128, 100, 0);
        const int chat_pct = (level >= 64) ? 100 : map<int>(level, 0, 64, 0, 100);

        return ChatmixResult {
            .level               = level,
            .game_volume_percent = game_pct,
            .chat_volume_percent = chat_pct,
        };
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        // SET (captured): 02 0c 06 00 09 1c 01 ff <lvl>
        // Only lvl 0x00 (0%) and 0x06 (100%) were observed → assume device range 0..6.
        const uint8_t device_level = map<uint8_t>(level, 0, 128, 0, 6);

        const std::array<uint8_t, 3> payload { 0x01, 0xff, device_level };
        auto r = sendRequest(device_handle, CMD_SIDETONE, HANDLE_SIDETONE, payload, /*read_reply=*/false);
        if (!r) {
            return r.error();
        }

        return SidetoneResult {
            .current_level = level,
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = 0,
            .device_max    = 6,
        };
    }

    Result<LightsResult> setLights(hid_device* device_handle, bool on) override
    {
        // SET (captured): 02 0c 04 00 0f 1c <brightness 0..100> — dock-station LED.
        // CAP_LIGHTS is on/off; map to full brightness or off.
        const std::array<uint8_t, 1> payload { static_cast<uint8_t>(on ? 100 : 0) };
        auto r = sendRequest(device_handle, CMD_BRIGHTNESS, HANDLE_BRIGHTNESS, payload, /*read_reply=*/false);
        if (!r) {
            return r.error();
        }

        return LightsResult {
            .enabled = on,
            .mode    = on ? "on" : "off",
        };
    }

    Result<NoiseFilterResult> setNoiseFilter(hid_device* device_handle, uint8_t level) override
    {
        // SET (captured): 02 0c 04 00 14 2d <v> — mic noise gate.
        // HeadsetControl level 0/1/2 (off/low/high) -> A50 Off/Night/Tournament.
        if (level > 2) {
            return DeviceError::invalidParameter("Noise filter level must be 0, 1, or 2");
        }
        const std::array<uint8_t, 1> payload { NOISE_LEVELS[level] };
        auto r = sendRequest(device_handle, CMD_NOISE_GATE, HANDLE_NOISE_GATE, payload, /*read_reply=*/false);
        if (!r) {
            return r.error();
        }

        return NoiseFilterResult { .level = level };
    }

    std::optional<ParametricEqualizerInfo> getParametricEqualizerInfo() const override
    {
        return ParametricEqualizerInfo {
            .bands_count  = EQ_BANDS,
            .gain_base    = 0.0f,
            .gain_step    = 1.0f / EQ_GAIN_UNITS_PER_DB, // 0.05 dB per device unit
            .gain_min     = EQ_GAIN_MIN_DB,
            .gain_max     = EQ_GAIN_MAX_DB,
            .q_factor_min = EQ_Q_MIN,
            .q_factor_max = EQ_Q_MAX,
            .freq_min     = EQ_FREQS.front(),
            .freq_max     = EQ_FREQS.back(),
            .filter_types = B(static_cast<int>(EqualizerFilterType::Peaking)),
        };
    }

    Result<ParametricEqualizerResult> setParametricEqualizer(
        hid_device* device_handle, const ParametricEqualizerSettings& settings) override
    {
        // SET (decoded from eq-audio.pcapng): 02 0c 38 00 0d 2c | 01 03 00 | 10×band
        // header `01 03 00` = target headphone; each band = [freqBE16][Q LE16][gainB].
        if (settings.size() != EQ_BANDS) {
            return DeviceError::invalidParameter("ASTRO A50 requires exactly 10 parametric EQ bands");
        }

        std::array<uint8_t, 3 + EQ_BANDS * 5> payload {};
        payload[0] = 0x01; // target: headphone
        payload[1] = 0x03; // constant (observed)
        payload[2] = 0x00;

        for (int i = 0; i < EQ_BANDS; ++i) {
            const auto& band = settings.bands[i];

            if (band.type != EqualizerFilterType::Peaking) {
                return DeviceError::invalidParameter("ASTRO A50 only supports peaking EQ bands");
            }
            if (band.frequency < EQ_FREQS.front() || band.frequency > EQ_FREQS.back()) {
                return DeviceError::invalidParameter("Frequency must be between 20 Hz and 20000 Hz");
            }
            if (band.gain < EQ_GAIN_MIN_DB || band.gain > EQ_GAIN_MAX_DB) {
                return DeviceError::invalidParameter("Gain must be between -6 dB and +6 dB");
            }
            if (band.q_factor < EQ_Q_MIN || band.q_factor > EQ_Q_MAX) {
                return DeviceError::invalidParameter("Q factor must be between 0.031 and 7.969");
            }

            const auto freq = static_cast<uint16_t>(std::lround(band.frequency));
            const auto qval = static_cast<uint16_t>(
                std::clamp<long>(std::lround(band.q_factor * EQ_Q_SCALE), 1, 0xffff));
            const auto gainb = static_cast<uint8_t>(std::clamp<long>(
                std::lround(EQ_GAIN_CENTER + band.gain * EQ_GAIN_UNITS_PER_DB), 0, 240));

            const size_t o = 3 + i * 5;
            payload[o + 0] = static_cast<uint8_t>(freq >> 8); // freq high (BE)
            payload[o + 1] = static_cast<uint8_t>(freq & 0xff); // freq low
            payload[o + 2] = static_cast<uint8_t>(qval & 0xff); // Q low (LE)
            payload[o + 3] = static_cast<uint8_t>(qval >> 8); // Q high
            payload[o + 4] = gainb; // gain
        }

        auto r = sendRequest(device_handle, CMD_EQ, HANDLE_EQ, payload, /*read_reply=*/false);
        if (!r) {
            return r.error();
        }

        return ParametricEqualizerResult {};
    }

private:
    /**
     * @brief Build a vendor frame, write it, and (optionally) read the matching reply.
     *
     * @param read_reply  When true, polls reads and returns the first 64-byte frame
     *                    whose marker matches and whose CMD byte equals @p cmd
     *                    (ignoring unrelated async push frames). When false, returns
     *                    an empty frame after a successful write (fire-and-forget SET).
     */
    [[nodiscard]] Result<std::array<uint8_t, FRAME_SIZE>> sendRequest(
        hid_device* device_handle,
        uint8_t cmd,
        uint8_t handle_byte,
        std::span<const uint8_t> payload,
        bool read_reply) const
    {
        std::array<uint8_t, FRAME_SIZE> frame {};
        frame[0] = REPORT_ID;
        frame[1] = MARKER;
        frame[2] = static_cast<uint8_t>(3 + payload.size()); // LEN counts byte[3..]
        frame[3] = 0x00;
        frame[4] = cmd;
        frame[5] = handle_byte;
        for (size_t i = 0; i < payload.size() && (6 + i) < FRAME_SIZE; ++i) {
            frame[6 + i] = payload[i];
        }

        if (auto w = writeHID(device_handle, frame, FRAME_SIZE); !w) {
            return w.error();
        }

        if (!read_reply) {
            return frame;
        }

        for (int attempt = 0; attempt < POLL_ATTEMPTS; ++attempt) {
            std::array<uint8_t, FRAME_SIZE> response {};
            auto read_result = readHIDTimeout(device_handle, response, hsc_device_timeout);
            if (!read_result) {
                return read_result.error();
            }
            if (*read_result == 0) {
                continue; // timed out with no data; retry
            }
            if (response[0] == REPORT_ID && response[1] == MARKER && response[4] == cmd) {
                return response;
            }
            // Otherwise it is an unrelated asynchronous push (volume, mic, BT, …);
            // keep polling for the frame that answers our command.
        }

        return DeviceError::timeout("ASTRO A50: no response for command");
    }
};

} // namespace headsetcontrol
