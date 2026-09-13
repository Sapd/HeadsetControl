#pragma once

#include "device_utils.hpp"
#include "hid_device.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Logitech ASTRO A50 Gen 4 base station — USB ID 9886:002c
 *
 * Logitech-era ASTRO product on Astro Gaming's legacy vendor ID. Verified on the
 * PlayStation/PC edition only; whether the Xbox/PC edition shares 002c is unknown. The
 * base's mode switch must be on PC: in console mode it re-enumerates as 9886:002b with
 * audio interfaces only and no HID. Hardware-verified on Linux only; Windows and macOS
 * are untested.
 *
 * Transport: one vendor HID interface (interface 6, usage page 0xFF32 / usage 0x74),
 * report ID 0x02, 64-byte frames, strictly one reply per request, no unsolicited frames
 * (unlike the Gen 5). After a timeout, the next request on that connection first waits
 * briefly for the outstanding reply and discards it. Command codes come from the
 * MIT-licensed eh-fifty project and were re-verified on hardware.
 *
 * Settings are not saved to flash: every write changes the active value only and is
 * lost when the base station power-cycles.
 *
 *   Request: 02 CMD [LEN PAYLOAD...] 00-padded
 *   Reply:   02 STATUS LEN PAYLOAD...    STATUS 0x02 = OK with payload,
 *            0x00 = accepted without payload, 0x01 = error (PAYLOAD[0] = code,
 *            PAYLOAD[4..] = NUL-terminated ASCII name). LEN is not trusted: the
 *            firmware occasionally reports a wrong value.
 *
 * Capabilities:
 * - Battery (0x7C): bit 7 charging, bits 0-6 percent; gated on the 0x54 link bit because
 *   the base serves the last known level after the headset powers off.
 * - Chatmix (0x72): 0 = full voice .. 255 = full game on a 5-step grid; same gating.
 * - Sidetone (0x62/0x68, slider 0x05) and microphone volume (0x62, slider 0x04): 0..100.
 * - Noise filter (0x64): HSC 0/1/2 -> Streaming / Night / Tournament.
 * - EQ preset (0x67/0x6C): three presets; the read-back lags the ack by a few hundred ms,
 *   so the setter polls until it agrees.
 * - Basic EQ (0x63): five gains on the active preset, preserving frequencies and bandwidths.
 * - Parametric EQ (0x6F bands, then 0x63 gains) on the active preset: five bands, gain
 *   byte = dB + 12 for -7..+7 dB, bandwidth = 4096 / Q, bands 1 and 5 are shelves (bw 0).
 *   Firmware enforces 80..15000 Hz (HID_ERROR_FC_NOT_VALID) but accepts any bandwidth;
 *   Q limits are derived from eh-fifty's bandwidth range: 4096/12288..4096/409.
 *
 * Not implemented:
 * - SAVE_VALUES (0x61) copies the active values into the saved ones (0x68 reports both) and
 *   is never sent; every write here changes the active value only.
 * - Auto-shutoff (0x74/0x78) and brightness (0x75/0x79) are eh-fifty's labels for two
 *   commands whose semantics could not be confirmed.
 * - Controls with no HeadsetControl capability, documented by eh-fifty and not exercised
 *   here: alert volume (0x76/0x7A), microphone EQ preset (0x71/0x7B), default balance
 *   (0x73/0x77), EQ preset names (0x6D/0x6E), device and firmware info (0x03, 0x55, 0xD6,
 *   0xDA), and the fourth noise gate mode HOME.
 * The device accepts out-of-range slider values unchanged (255 reads back as 255), so this
 * class clamps before writing.
 */
class LogitechAstroA50Gen4 : public HIDDevice {
public:
    static constexpr uint16_t VENDOR_ASTRO = 0x9886;
    static constexpr std::array<uint16_t, 1> PRODUCT_IDS { 0x002c };

    // Frame structure
    static constexpr uint8_t REPORT_ID     = 0x02;
    static constexpr size_t FRAME_SIZE     = 64;
    static constexpr size_t PAYLOAD_OFFSET = 3;

    // Reply status (byte 1)
    static constexpr uint8_t STATUS_ACCEPTED = 0x00;
    static constexpr uint8_t STATUS_ERROR    = 0x01;
    static constexpr uint8_t STATUS_OK       = 0x02;

    // Commands
    static constexpr uint8_t CMD_HEADSET_STATUS = 0x54;
    static constexpr uint8_t CMD_SET_SLIDER     = 0x62;
    static constexpr uint8_t CMD_SET_EQ_GAIN    = 0x63;
    static constexpr uint8_t CMD_SET_NOISE_GATE = 0x64;
    static constexpr uint8_t CMD_SET_EQ_PRESET  = 0x67;
    static constexpr uint8_t CMD_GET_SLIDER     = 0x68;
    static constexpr uint8_t CMD_GET_EQ_PRESET  = 0x6C;
    static constexpr uint8_t CMD_SET_EQ_BAND    = 0x6F;
    static constexpr uint8_t CMD_GET_BALANCE    = 0x72;
    static constexpr uint8_t CMD_GET_BATTERY    = 0x7C;

    // Slider ids
    static constexpr uint8_t SLIDER_MIC      = 0x04;
    static constexpr uint8_t SLIDER_SIDETONE = 0x05;
    static constexpr uint8_t SLIDER_MAX      = 100;

    // 0x54 status: bit 0 = docked, bit 1 = live link to the headset
    static constexpr uint8_t STATUS_LINKED = 0x02;

    // Balance (0x72): 0 = full voice, 255 = full game, moves on a 5-step grid.
    // The physical midpoint reads 130 (nearest grid point to 127.5), so raw values
    // within one grid step of centre are reported as exactly balanced.
    static constexpr int BALANCE_MAX         = 255;
    static constexpr int BALANCE_CENTRE_LOW  = 125;
    static constexpr int BALANCE_CENTRE_HIGH = 130;

    // Noise gate: device has 4 modes; HeadsetControl 0/1/2 -> Streaming / Night / Tournament
    static constexpr std::array<uint8_t, 3> NOISE_LEVELS { 0x00, 0x01, 0x03 };

    // Equalizer: presets are numbered 1..3 on the wire (HeadsetControl 0..2). Each preset has
    // five bands; gain byte = dB + 12; bandwidth = (BW / f0) × 4096, 0 for the shelf bands.
    static constexpr uint8_t EQ_PRESETS       = 3;
    static constexpr int EQ_BANDS             = 5;
    static constexpr int EQ_GAIN_OFFSET       = 12;
    static constexpr float EQ_GAIN_MIN_DB     = -7.0f;
    static constexpr float EQ_GAIN_MAX_DB     = 7.0f;
    static constexpr float EQ_BW_SCALE        = 4096.0f;
    static constexpr uint16_t EQ_BW_MIN       = 409; // 0.1 × f0
    static constexpr uint16_t EQ_BW_MAX       = 12288; // 3.0 × f0
    static constexpr float EQ_Q_MIN           = EQ_BW_SCALE / EQ_BW_MAX; // ≈ 0.33
    static constexpr float EQ_Q_MAX           = EQ_BW_SCALE / EQ_BW_MIN; // ≈ 10
    static constexpr int EQ_FREQ_MIN          = 80;
    static constexpr int EQ_FREQ_MAX          = 15000;
    static constexpr int EQ_PRESET_POLL_MS    = 200;
    static constexpr int EQ_PRESET_POLL_LIMIT = 8;

    uint16_t getVendorId() const override { return VENDOR_ASTRO; }

    std::vector<uint16_t> getProductIds() const override
    {
        return { PRODUCT_IDS.begin(), PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override { return "Logitech ASTRO A50 Gen 4"sv; }

    constexpr int getCapabilities() const override
    {
        return B(CAP_BATTERY_STATUS) | B(CAP_CHATMIX_STATUS) | B(CAP_SIDETONE)
            | B(CAP_SIDETONE_STATUS) | B(CAP_MICROPHONE_VOLUME) | B(CAP_NOISE_FILTER)
            | B(CAP_EQUALIZER_PRESET) | B(CAP_EQUALIZER) | B(CAP_PARAMETRIC_EQUALIZER);
    }

    uint8_t getEqualizerPresetsCount() const override { return EQ_PRESETS; }

    std::optional<EqualizerInfo> getEqualizerInfo() const override
    {
        return EqualizerInfo {
            .bands_count    = EQ_BANDS,
            .bands_baseline = 0,
            .bands_step     = 1.0f,
            .bands_min      = static_cast<int>(EQ_GAIN_MIN_DB),
            .bands_max      = static_cast<int>(EQ_GAIN_MAX_DB),
        };
    }

    void onConnectionClosed(hid_device* device_handle) const override
    {
        const std::lock_guard lock(request_mutex_);
        pending_replies_.erase(device_handle);
    }

    std::optional<ParametricEqualizerInfo> getParametricEqualizerInfo() const override
    {
        return ParametricEqualizerInfo {
            .bands_count  = EQ_BANDS,
            .gain_base    = 0.0f,
            .gain_step    = 1.0f,
            .gain_min     = EQ_GAIN_MIN_DB,
            .gain_max     = EQ_GAIN_MAX_DB,
            .q_factor_min = EQ_Q_MIN,
            .q_factor_max = EQ_Q_MAX,
            .freq_min     = EQ_FREQ_MIN,
            .freq_max     = EQ_FREQ_MAX,
            .filter_types = B(static_cast<int>(EqualizerFilterType::LowShelf))
                | B(static_cast<int>(EqualizerFilterType::Peaking))
                | B(static_cast<int>(EqualizerFilterType::HighShelf)),
        };
    }

    constexpr capability_detail getCapabilityDetail([[maybe_unused]] enum capabilities cap) const override
    {
        // Control lives on the vendor HID: interface 6 (Linux/macOS),
        // usage page 0xFF32 / usage 0x74 (Windows).
        return { .usagepage = 0xff32, .usageid = 0x0074, .interface_id = 6 };
    }

    // ------------------------------------------------------------------------
    // Pure helpers (public so they can be unit-tested without hardware)
    // ------------------------------------------------------------------------

    // Build a 64-byte request frame: 02 CMD [LEN PAYLOAD...] zero-padded.
    [[nodiscard]] static std::array<uint8_t, FRAME_SIZE> buildFrame(uint8_t cmd, std::span<const uint8_t> payload)
    {
        std::array<uint8_t, FRAME_SIZE> frame {};
        frame[0] = REPORT_ID;
        frame[1] = cmd;
        if (!payload.empty()) {
            frame[2] = static_cast<uint8_t>(payload.size());
            for (size_t i = 0; i < payload.size() && (PAYLOAD_OFFSET + i) < FRAME_SIZE; ++i) {
                frame[PAYLOAD_OFFSET + i] = payload[i];
            }
        }
        return frame;
    }

    // Battery byte (0x7C payload[0]): bit 7 = charging, bits 0-6 = percent.
    [[nodiscard]] static constexpr int batteryPercent(uint8_t raw) { return raw & 0x7f; }
    [[nodiscard]] static constexpr bool batteryCharging(uint8_t raw) { return (raw & 0x80) != 0; }

    // 0x54 payload[0] bit 1: the base has a live link to the headset.
    [[nodiscard]] static constexpr bool isLinked(uint8_t status) { return (status & STATUS_LINKED) != 0; }

    // Map a raw balance (0 = voice .. 255 = game) to HeadsetControl's 0..128 (< 64 = game).
    [[nodiscard]] static constexpr int balanceToLevel(uint8_t raw)
    {
        if (raw >= BALANCE_CENTRE_LOW && raw <= BALANCE_CENTRE_HIGH) {
            return 64;
        }
        return map<int>(raw, 0, BALANCE_MAX, 128, 0);
    }

    // Gain byte for the 0x63 payload: dB + 12, so -7..+7 dB -> 5..19.
    [[nodiscard]] static uint8_t gainToByte(float gain_db)
    {
        return static_cast<uint8_t>(std::lround(gain_db) + EQ_GAIN_OFFSET);
    }

    // Bandwidth field for the 0x6F payload: (BW / f0) × 4096 = 4096 / Q. The caller validates Q;
    // the clamp only guards the rounding at the edges of the range.
    [[nodiscard]] static uint16_t qToBandwidth(float q_factor)
    {
        const auto raw = std::lround(EQ_BW_SCALE / q_factor);
        return static_cast<uint16_t>(std::clamp<long>(raw, EQ_BW_MIN, EQ_BW_MAX));
    }

    // The filter type each band must have: shelves at the edges, peaking in between.
    [[nodiscard]] static constexpr EqualizerFilterType bandType(int band)
    {
        if (band == 0) {
            return EqualizerFilterType::LowShelf;
        }
        if (band == EQ_BANDS - 1) {
            return EqualizerFilterType::HighShelf;
        }
        return EqualizerFilterType::Peaking;
    }

    // Extract the NUL-terminated ASCII error name from an error reply.
    [[nodiscard]] static std::string errorName(std::span<const uint8_t, FRAME_SIZE> reply)
    {
        constexpr size_t NAME_OFFSET = PAYLOAD_OFFSET + 4;
        std::string name;
        for (size_t i = NAME_OFFSET; i < FRAME_SIZE && reply[i] != 0; ++i) {
            const auto c = reply[i];
            name.push_back((c >= 0x20 && c < 0x7f) ? static_cast<char>(c) : '?');
        }
        return name;
    }

    // ------------------------------------------------------------------------
    // Capabilities
    // ------------------------------------------------------------------------

    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        auto linked = queryLinked(device_handle);
        if (!linked) {
            return linked.error();
        }
        if (!*linked) {
            // The base keeps serving the last known level after the headset powers off.
            return BatteryResult { .level_percent = -1, .status = BATTERY_UNAVAILABLE };
        }

        auto reply = sendRequest(device_handle, CMD_GET_BATTERY, {}, /*expect_payload=*/true);
        if (!reply) {
            return reply.error();
        }

        const uint8_t raw = (*reply)[PAYLOAD_OFFSET];
        return BatteryResult {
            .level_percent = batteryPercent(raw),
            .status        = batteryCharging(raw) ? BATTERY_CHARGING : BATTERY_AVAILABLE,
        };
    }

    Result<ChatmixResult> getChatmix(hid_device* device_handle) override
    {
        auto linked = queryLinked(device_handle);
        if (!linked) {
            return linked.error();
        }
        if (!*linked) {
            // 0x72 returns a frozen, impossible value (0xfd) with no link.
            return DeviceError::deviceOffline("ASTRO A50 Gen 4: headset is not linked to the base station");
        }

        auto reply = sendRequest(device_handle, CMD_GET_BALANCE, {}, /*expect_payload=*/true);
        if (!reply) {
            return reply.error();
        }

        const int level    = balanceToLevel((*reply)[PAYLOAD_OFFSET]);
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
        const uint8_t device_level = map<uint8_t>(level, 0, 128, 0, SLIDER_MAX);
        if (auto r = setSlider(device_handle, SLIDER_SIDETONE, device_level); !r) {
            return r.error();
        }

        return SidetoneResult {
            .current_level = level,
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = 0,
            .device_max    = SLIDER_MAX,
            .device_level  = device_level,
        };
    }

    Result<SidetoneResult> getSidetone(hid_device* device_handle) override
    {
        // Reply payload: 68 <slider> <active> <saved>
        const std::array<uint8_t, 1> payload { SLIDER_SIDETONE };
        auto reply = sendRequest(device_handle, CMD_GET_SLIDER, payload, /*expect_payload=*/true);
        if (!reply) {
            return reply.error();
        }
        if ((*reply)[PAYLOAD_OFFSET] != CMD_GET_SLIDER || (*reply)[PAYLOAD_OFFSET + 1] != SLIDER_SIDETONE) {
            return DeviceError::protocolError("ASTRO A50 Gen 4: unexpected slider reply");
        }

        const uint8_t device_level = (*reply)[PAYLOAD_OFFSET + 2];
        return SidetoneResult {
            .current_level = map<uint8_t>(device_level, 0, SLIDER_MAX, 0, 128),
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = 0,
            .device_max    = SLIDER_MAX,
            .device_level  = device_level,
        };
    }

    Result<MicVolumeResult> setMicVolume(hid_device* device_handle, uint8_t volume) override
    {
        const uint8_t device_level = map<uint8_t>(volume, 0, 128, 0, SLIDER_MAX);
        if (auto r = setSlider(device_handle, SLIDER_MIC, device_level); !r) {
            return r.error();
        }

        return MicVolumeResult {
            .volume     = volume,
            .min_volume = 0,
            .max_volume = 128,
        };
    }

    Result<NoiseFilterResult> setNoiseFilter(hid_device* device_handle, uint8_t level) override
    {
        if (level >= NOISE_LEVELS.size()) {
            return DeviceError::invalidParameter("Noise filter level must be 0, 1, or 2");
        }

        // Reply payload[0] echoes the mode, not the command.
        const std::array<uint8_t, 1> payload { NOISE_LEVELS[level] };
        auto reply = sendRequest(device_handle, CMD_SET_NOISE_GATE, payload, /*expect_payload=*/false);
        if (!reply) {
            return reply.error();
        }
        if ((*reply)[1] == STATUS_OK && (*reply)[PAYLOAD_OFFSET] != NOISE_LEVELS[level]) {
            return DeviceError::protocolError("ASTRO A50 Gen 4: noise gate mode was not echoed back");
        }

        return NoiseFilterResult { .level = level };
    }

    Result<EqualizerPresetResult> setEqualizerPreset(hid_device* device_handle, uint8_t preset) override
    {
        if (preset >= EQ_PRESETS) {
            return DeviceError::invalidParameter("ASTRO A50 Gen 4 has presets 0, 1 and 2");
        }
        const uint8_t device_preset = static_cast<uint8_t>(preset + 1);

        // Reply payload: 67 <preset>
        const std::array<uint8_t, 1> payload { device_preset };
        auto reply = sendRequest(device_handle, CMD_SET_EQ_PRESET, payload, /*expect_payload=*/false);
        if (!reply) {
            return reply.error();
        }
        if ((*reply)[1] == STATUS_OK && (*reply)[PAYLOAD_OFFSET + 1] != device_preset) {
            return DeviceError::protocolError("ASTRO A50 Gen 4: preset write was not echoed back");
        }

        // The base acknowledges immediately but reports the old preset for a few hundred
        // milliseconds. Poll until the read-back agrees rather than trusting the echo.
        for (int attempt = 0; attempt < EQ_PRESET_POLL_LIMIT; ++attempt) {
            std::this_thread::sleep_for(std::chrono::milliseconds(EQ_PRESET_POLL_MS));
            auto active = readActivePreset(device_handle);
            if (!active) {
                return active.error();
            }
            if (*active == device_preset) {
                return EqualizerPresetResult { .preset = preset, .total_presets = EQ_PRESETS };
            }
        }
        return DeviceError::protocolError("ASTRO A50 Gen 4: preset change was not confirmed by the base station");
    }

    Result<EqualizerResult> setEqualizer(hid_device* device_handle, const EqualizerSettings& settings) override
    {
        if (settings.size() != EQ_BANDS) {
            return DeviceError::invalidParameter("ASTRO A50 Gen 4 requires exactly 5 equalizer gains");
        }
        for (float gain : settings.bands) {
            if (!validGain(gain)) {
                return DeviceError::invalidParameter("Gain must be finite and between -7 dB and +7 dB");
            }
        }
        auto active = readActivePreset(device_handle);
        if (!active) {
            return active.error();
        }
        if (auto result = setEqGains(device_handle, *active, settings.bands); !result) {
            return result.error();
        }
        return EqualizerResult {};
    }

    Result<ParametricEqualizerResult> setParametricEqualizer(
        hid_device* device_handle, const ParametricEqualizerSettings& settings) override
    {
        if (settings.size() != EQ_BANDS) {
            return DeviceError::invalidParameter("ASTRO A50 Gen 4 requires exactly 5 parametric EQ bands");
        }
        for (int i = 0; i < EQ_BANDS; ++i) {
            const auto& band = settings.bands[i];
            if (band.type != bandType(i)) {
                return DeviceError::invalidParameter(
                    "ASTRO A50 Gen 4 bands must be lowshelf, peaking, peaking, peaking, highshelf");
            }
            if (!std::isfinite(band.frequency) || band.frequency < EQ_FREQ_MIN || band.frequency > EQ_FREQ_MAX) {
                return DeviceError::invalidParameter("Frequency must be between 80 Hz and 15000 Hz");
            }
            if (!validGain(band.gain)) {
                return DeviceError::invalidParameter("Gain must be between -7 dB and +7 dB");
            }
            if (band.type == EqualizerFilterType::Peaking && (!std::isfinite(band.q_factor) || band.q_factor < EQ_Q_MIN || band.q_factor > EQ_Q_MAX)) {
                return DeviceError::invalidParameter(
                    std::format("Q factor must be finite and between {} and {}", EQ_Q_MIN, EQ_Q_MAX));
            }
        }

        // Bands are stored per preset, so write to whichever preset is active.
        auto active = readActivePreset(device_handle);
        if (!active) {
            return active.error();
        }
        const uint8_t device_preset = *active;

        // 0x6F payload: <preset> <band 1..5> <bandwidth LE16> <frequency LE16>; reply payload: 6F ...
        for (int i = 0; i < EQ_BANDS; ++i) {
            const auto& band  = settings.bands[i];
            const uint16_t bw = (band.type == EqualizerFilterType::Peaking) ? qToBandwidth(band.q_factor) : 0;
            const auto freq   = static_cast<uint16_t>(std::lround(band.frequency));
            const std::array<uint8_t, 6> payload {
                device_preset,
                static_cast<uint8_t>(i + 1),
                static_cast<uint8_t>(bw & 0xff),
                static_cast<uint8_t>(bw >> 8),
                static_cast<uint8_t>(freq & 0xff),
                static_cast<uint8_t>(freq >> 8),
            };
            auto reply = sendRequest(device_handle, CMD_SET_EQ_BAND, payload, /*expect_payload=*/false);
            if (!reply) {
                return reply.error();
            }
            if ((*reply)[1] == STATUS_OK && (*reply)[PAYLOAD_OFFSET] != CMD_SET_EQ_BAND) {
                return DeviceError::protocolError("ASTRO A50 Gen 4: EQ band write was not echoed back");
            }
        }

        std::array<float, EQ_BANDS> gains {};
        for (int i = 0; i < EQ_BANDS; ++i) {
            gains[i] = settings.bands[i].gain;
        }
        if (auto result = setEqGains(device_handle, device_preset, gains); !result) {
            return result.error();
        }

        return ParametricEqualizerResult {};
    }

private:
    mutable std::mutex request_mutex_;
    mutable std::unordered_set<hid_device*> pending_replies_;

    [[nodiscard]] static bool validGain(float gain)
    {
        return std::isfinite(gain) && gain >= EQ_GAIN_MIN_DB && gain <= EQ_GAIN_MAX_DB;
    }

    // Both EQ entry points validate all gains before any I/O. Only gains are changed here.
    [[nodiscard]] Result<void> setEqGains(hid_device* device_handle, uint8_t preset, std::span<const float> gains) const
    {
        std::array<uint8_t, 1 + EQ_BANDS> payload { preset };
        for (int i = 0; i < EQ_BANDS; ++i) {
            payload[1 + i] = gainToByte(gains[i]);
        }
        auto reply = sendRequest(device_handle, CMD_SET_EQ_GAIN, payload, /*expect_payload=*/false);
        if (!reply) {
            return reply.error();
        }
        if ((*reply)[1] == STATUS_OK
            && ((*reply)[PAYLOAD_OFFSET] != CMD_SET_EQ_GAIN || (*reply)[PAYLOAD_OFFSET + 1] != preset)) {
            return DeviceError::protocolError("ASTRO A50 Gen 4: EQ gain write was not echoed back");
        }
        return {};
    }

    // Query 0x6C and return the active preset as numbered on the wire (1..3).
    [[nodiscard]] Result<uint8_t> readActivePreset(hid_device* device_handle) const
    {
        auto reply = sendRequest(device_handle, CMD_GET_EQ_PRESET, {}, /*expect_payload=*/true);
        if (!reply) {
            return reply.error();
        }
        const uint8_t preset = (*reply)[PAYLOAD_OFFSET];
        if (preset < 1 || preset > EQ_PRESETS) {
            return DeviceError::protocolError("ASTRO A50 Gen 4: active preset out of range");
        }
        return preset;
    }

    // Write one slider's active value. Reply payload: 62 <slider>.
    [[nodiscard]] Result<void> setSlider(hid_device* device_handle, uint8_t slider, uint8_t value) const
    {
        const std::array<uint8_t, 2> payload { slider, value };
        auto reply = sendRequest(device_handle, CMD_SET_SLIDER, payload, /*expect_payload=*/false);
        if (!reply) {
            return reply.error();
        }
        if ((*reply)[1] == STATUS_OK && (*reply)[PAYLOAD_OFFSET + 1] != slider) {
            return DeviceError::protocolError("ASTRO A50 Gen 4: slider write was not echoed back");
        }
        return {};
    }

    // Query 0x54 and return whether the base has a live link to the headset.
    [[nodiscard]] Result<bool> queryLinked(hid_device* device_handle) const
    {
        auto reply = sendRequest(device_handle, CMD_HEADSET_STATUS, {}, /*expect_payload=*/true);
        if (!reply) {
            return reply.error();
        }
        return isLinked((*reply)[PAYLOAD_OFFSET]);
    }

    /**
     * @brief Build a frame, write it, read the single reply and validate its status.
     *
     * @param expect_payload  When true, a STATUS_ACCEPTED reply (no data) is an error.
     * @return The full 64-byte reply; payload starts at PAYLOAD_OFFSET.
     */
    [[nodiscard]] Result<std::array<uint8_t, FRAME_SIZE>> sendRequest(
        hid_device* device_handle,
        uint8_t cmd,
        std::span<const uint8_t> payload,
        bool expect_payload) const
    {
        const std::lock_guard lock(request_mutex_);
        if (pending_replies_.contains(device_handle)) {
            // Several reply layouts carry no command identifier, so give the outstanding
            // reply one bounded chance to arrive and discard it. Proceed either way: a reply
            // that never comes (lost, or a handle address reused without onConnectionClosed())
            // must not block the connection forever. Every supported command answers within
            // milliseconds; a later frame would be read by the next request, which the
            // per-command echo checks catch for the setters.
            constexpr int RECOVERY_TIMEOUT_MS = 1000;
            std::array<uint8_t, FRAME_SIZE> stale {};
            static_cast<void>(readHIDTimeout(device_handle, stale, RECOVERY_TIMEOUT_MS));
            pending_replies_.erase(device_handle);
        }

        const auto frame = buildFrame(cmd, payload);
        if (auto w = writeHID(device_handle, frame, FRAME_SIZE); !w) {
            return w.error();
        }

        std::array<uint8_t, FRAME_SIZE> reply {};
        auto read_result = readHIDTimeout(device_handle, reply, hsc_device_timeout);
        if (!read_result) {
            pending_replies_.insert(device_handle);
            return read_result.error();
        }
        if (*read_result == 0) {
            pending_replies_.insert(device_handle);
            return DeviceError::timeout("ASTRO A50 Gen 4: no reply from base station");
        }

        if (*read_result != FRAME_SIZE) {
            return DeviceError::protocolError("ASTRO A50 Gen 4: incomplete reply from base station");
        }
        if (reply[0] != REPORT_ID) {
            return DeviceError::protocolError("ASTRO A50 Gen 4: reply does not start with report ID 0x02");
        }

        switch (reply[1]) {
        case STATUS_OK:
            return reply;
        case STATUS_ACCEPTED:
            if (expect_payload) {
                return DeviceError::protocolError("ASTRO A50 Gen 4: base station returned no data");
            }
            return reply;
        case STATUS_ERROR:
            return DeviceError::protocolError(
                std::format("ASTRO A50 Gen 4: {} (code 0x{:02x})", errorName(reply), reply[PAYLOAD_OFFSET]));
        default:
            return DeviceError::protocolError("ASTRO A50 Gen 4: unknown reply status");
        }
    }
};

} // namespace headsetcontrol
