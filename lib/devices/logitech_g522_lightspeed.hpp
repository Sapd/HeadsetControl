#pragma once

#include "../utility.hpp"
#include "device.hpp"
#include "protocols/logitech_centurion_protocol.hpp"
#include "result_types.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Logitech G522 LIGHTSPEED (PID 0x0b18)
 *
 * The G522 speaks the Logitech Centurion protocol on usage page 0xffa0, like the
 * G PRO X 2 LIGHTSPEED, but its frames start with 0x50 0x23 instead of 0x51.
 * Feature indexes are discovered at runtime through the dongle's bridge.
 */
class LogitechG522Lightspeed : public protocols::LogitechCenturionProtocol {
public:
    static constexpr std::array<uint16_t, 1> SUPPORTED_PRODUCT_IDS { 0x0b18 };
    static constexpr std::array<uint8_t, 2> FRAME_PREFIX { 0x50, 0x23 };
    static constexpr uint8_t SIDETONE_DEVICE_MAX = 9;
    static constexpr uint8_t SIDETONE_MIC_ID     = 0x01;

    // Lighting effect parameters (feature 0x0621, function 3): zone 00 00, effect, effect data.
    // "On" is G HUB's default look: two-zone effect 04, zone 1 Logitech blue 00b8fc,
    // zone 2 magenta ff00ab, brightness 100. "Off" is the fixed effect 00 with color 000000.
    static constexpr std::array<uint8_t, 10> LIGHTS_ON_PARAMS { 0x00, 0x00, 0x04, 0x00, 0xb8, 0xfc, 0xff, 0x00, 0xab, 0x64 };
    static constexpr std::array<uint8_t, 10> LIGHTS_OFF_PARAMS {};

    // Playback equalizer (feature 0x020d, function 2): 10 peaking bands, each encoded as
    // <frequency Hz, 16-bit BE> <Q x 32> <gain: dB x 20 + 120, 16-bit BE>. G HUB shows the
    // ranges below: gain -6..+6 dB in 0.05 dB steps, Q 0.031..7.969.
    struct EqBand {
        uint16_t frequency = 0;
        float gain_db      = 0.0f;
        float q_factor     = 0.0f;
    };

    static constexpr size_t EQ_BANDS          = 10;
    static constexpr float EQ_GAIN_MIN        = -6.0f;
    static constexpr float EQ_GAIN_MAX        = 6.0f;
    static constexpr float EQ_GAIN_STEP       = 0.05f;
    static constexpr int EQ_GAIN_ZERO_RAW     = 120;
    static constexpr float EQ_Q_SCALE         = 32.0f;
    static constexpr float EQ_Q_MIN           = 1.0f / EQ_Q_SCALE;
    static constexpr float EQ_Q_MAX           = 255.0f / EQ_Q_SCALE;
    static constexpr float EQ_DEFAULT_Q       = 22.0f / EQ_Q_SCALE; // 0.688 in G HUB
    static constexpr uint16_t EQ_FREQ_MIN     = 20;
    static constexpr uint16_t EQ_FREQ_MAX     = 20000;
    static constexpr size_t EQ_PARAMS_SIZE    = 3 + EQ_BANDS * 5;
    static constexpr uint8_t EQ_PRESETS_COUNT = 5;

    using EqBands = std::array<EqBand, EQ_BANDS>;

    // G HUB's built-in presets, as G HUB displays them.
    static constexpr EqBands EQ_PRESET_DEFAULT { { { 20, 0.0f, EQ_DEFAULT_Q }, { 50, 0.0f, EQ_DEFAULT_Q },
        { 125, 0.0f, EQ_DEFAULT_Q }, { 250, 0.0f, EQ_DEFAULT_Q }, { 500, 0.0f, EQ_DEFAULT_Q },
        { 1000, 0.0f, EQ_DEFAULT_Q }, { 2500, 0.0f, EQ_DEFAULT_Q }, { 5000, 0.0f, EQ_DEFAULT_Q },
        { 10000, 0.0f, EQ_DEFAULT_Q }, { 20000, 0.0f, EQ_DEFAULT_Q } } };
    static constexpr EqBands EQ_PRESET_BASS_BOOST { { { 20, 3.0f, EQ_DEFAULT_Q }, { 50, 2.5f, EQ_DEFAULT_Q },
        { 125, 1.75f, EQ_DEFAULT_Q }, { 250, 1.0f, 1.0f }, { 500, 0.0f, EQ_DEFAULT_Q },
        { 1000, 0.0f, EQ_DEFAULT_Q }, { 2500, 0.0f, EQ_DEFAULT_Q }, { 5000, 0.0f, EQ_DEFAULT_Q },
        { 10000, 0.0f, EQ_DEFAULT_Q }, { 20000, 0.0f, EQ_DEFAULT_Q } } };
    static constexpr EqBands EQ_PRESET_GAMING { { { 20, 0.0f, EQ_DEFAULT_Q }, { 50, 0.0f, EQ_DEFAULT_Q },
        { 250, 0.5f, EQ_DEFAULT_Q }, { 400, 0.5f, EQ_DEFAULT_Q }, { 800, 0.75f, EQ_DEFAULT_Q },
        { 1500, 1.0f, EQ_DEFAULT_Q }, { 2500, 1.25f, EQ_DEFAULT_Q }, { 5000, 1.5f, EQ_DEFAULT_Q },
        { 10000, 1.75f, EQ_DEFAULT_Q }, { 19000, 2.75f, EQ_DEFAULT_Q } } };
    static constexpr EqBands EQ_PRESET_GAMING_FPS { { { 20, 0.0f, EQ_DEFAULT_Q }, { 50, 0.0f, EQ_DEFAULT_Q },
        { 125, 0.0f, EQ_DEFAULT_Q }, { 400, -1.0f, EQ_DEFAULT_Q }, { 800, -1.0f, EQ_DEFAULT_Q },
        { 1250, 3.75f, 2.0f }, { 2500, 4.5f, 1.0f }, { 5000, 1.75f, EQ_DEFAULT_Q },
        { 10000, 1.5f, EQ_DEFAULT_Q }, { 19000, 1.5f, EQ_DEFAULT_Q } } };
    static constexpr EqBands EQ_PRESET_MEDIA { { { 20, 4.0f, EQ_DEFAULT_Q }, { 50, 3.0f, EQ_DEFAULT_Q },
        { 125, 2.0f, EQ_DEFAULT_Q }, { 250, -0.5f, EQ_DEFAULT_Q }, { 500, -0.5f, EQ_DEFAULT_Q },
        { 1000, 1.0f, EQ_DEFAULT_Q }, { 2500, 2.0f, EQ_DEFAULT_Q }, { 5000, 1.5f, EQ_DEFAULT_Q },
        { 10000, 0.5f, EQ_DEFAULT_Q }, { 20000, -0.5f, EQ_DEFAULT_Q } } };

    static constexpr std::array<const EqBands*, EQ_PRESETS_COUNT> EQ_PRESETS {
        &EQ_PRESET_DEFAULT, &EQ_PRESET_BASS_BOOST, &EQ_PRESET_GAMING, &EQ_PRESET_GAMING_FPS, &EQ_PRESET_MEDIA
    };
    static constexpr std::array<std::string_view, EQ_PRESETS_COUNT> EQ_PRESET_NAMES {
        "Default"sv, "Bass Boost"sv, "Gaming"sv, "Gaming - FPS"sv, "Media"sv
    };

    constexpr uint16_t getVendorId() const override
    {
        return VENDOR_LOGITECH;
    }

    std::vector<uint16_t> getProductIds() const override
    {
        return { SUPPORTED_PRODUCT_IDS.begin(), SUPPORTED_PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "Logitech G522 LIGHTSPEED"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_LIGHTS) | B(CAP_INACTIVE_TIME) | B(CAP_VOICE_PROMPTS)
            | B(CAP_EQUALIZER_PRESET) | B(CAP_EQUALIZER) | B(CAP_PARAMETRIC_EQUALIZER)
            | B(CAP_MICROPHONE_MUTE_LED_BRIGHTNESS) | B(CAP_SIDETONE_STATUS);
    }

    uint8_t getEqualizerPresetsCount() const override
    {
        return EQ_PRESETS_COUNT;
    }

    std::optional<EqualizerPresets> getEqualizerPresets() const override
    {
        EqualizerPresets presets;
        for (size_t i = 0; i < EQ_PRESETS_COUNT; ++i) {
            std::vector<float> gains;
            gains.reserve(EQ_BANDS);
            for (const auto& band : *EQ_PRESETS[i]) {
                gains.push_back(band.gain_db);
            }
            presets.presets.push_back({ std::string(EQ_PRESET_NAMES[i]), std::move(gains) });
        }
        return presets;
    }

    std::optional<EqualizerInfo> getEqualizerInfo() const override
    {
        return EqualizerInfo {
            .bands_count    = static_cast<int>(EQ_BANDS),
            .bands_baseline = 0,
            .bands_step     = EQ_GAIN_STEP,
            .bands_min      = static_cast<int>(EQ_GAIN_MIN),
            .bands_max      = static_cast<int>(EQ_GAIN_MAX)
        };
    }

    std::optional<ParametricEqualizerInfo> getParametricEqualizerInfo() const override
    {
        return ParametricEqualizerInfo {
            .bands_count  = static_cast<int>(EQ_BANDS),
            .gain_base    = 0.0f,
            .gain_step    = EQ_GAIN_STEP,
            .gain_min     = EQ_GAIN_MIN,
            .gain_max     = EQ_GAIN_MAX,
            .q_factor_min = EQ_Q_MIN,
            .q_factor_max = EQ_Q_MAX,
            .freq_min     = EQ_FREQ_MIN,
            .freq_max     = EQ_FREQ_MAX,
            .filter_types = B(static_cast<int>(EqualizerFilterType::Peaking))
        };
    }

    constexpr capability_detail getCapabilityDetail(enum capabilities cap) const override
    {
        switch (cap) {
        case CAP_BATTERY_STATUS:
        case CAP_SIDETONE:
        case CAP_SIDETONE_STATUS:
        case CAP_LIGHTS:
        case CAP_INACTIVE_TIME:
        case CAP_VOICE_PROMPTS:
        case CAP_EQUALIZER_PRESET:
        case CAP_EQUALIZER:
        case CAP_PARAMETRIC_EQUALIZER:
        case CAP_MICROPHONE_MUTE_LED_BRIGHTNESS:
            return { .usagepage = 0xffa0, .usageid = 0x0001, .interface_id = 3 };
        default:
            return HIDDevice::getCapabilityDetail(cap);
        }
    }

    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        auto start_time = std::chrono::steady_clock::now();

        auto battery = sendCenturionFeatureRequest(
            device_handle,
            static_cast<uint16_t>(protocols::CenturionFeature::CenturionBatterySoc),
            0x00);
        if (!battery) {
            return battery.error();
        }

        auto battery_result = parseCenturionBatteryResponse(*battery);
        if (!battery_result) {
            return battery_result.error();
        }

        battery_result->raw_data       = *battery;
        auto end_time                  = std::chrono::steady_clock::now();
        battery_result->query_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        return *battery_result;
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        // INFO: The original G HUB app does some strange mapping:
        //   0 -   5 -> 0x00 (off)
        //   6 -  16 -> 0x01
        //  17 -  27 -> 0x02
        //  28 -  38 -> 0x03
        //  39 -  49 -> 0x04
        //  50 -  61 -> 0x05
        //  62 -  72 -> 0x06
        //  73 -  83 -> 0x07
        //  84 -  94 -> 0x08
        //  95 - 100 -> 0x09
        uint8_t mapped = map<uint8_t>(level, 0, 128, 0, SIDETONE_DEVICE_MAX);

        if (auto write_result = sendCenturionFeatureRequest(
                device_handle,
                static_cast<uint16_t>(protocols::CenturionFeature::HeadsetAudioSidetone),
                0x10,
                std::array<uint8_t, 3> { SIDETONE_MIC_ID, 0xFF, mapped });
            !write_result) {
            return write_result.error();
        }

        return SidetoneResult {
            .current_level = level,
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = 0,
            .device_max    = SIDETONE_DEVICE_MAX
        };
    }

    Result<SidetoneResult> getSidetone(hid_device* device_handle) override
    {
        auto reply = sendCenturionFeatureRequest(
            device_handle,
            static_cast<uint16_t>(protocols::CenturionFeature::HeadsetAudioSidetone),
            0x00);
        if (!reply) {
            return reply.error();
        }
        return parseSidetoneResponse(*reply);
    }

    /**
     * @brief Parse the sidetone read reply: <mic id> <?> <gain?> <level 0-9>.
     *
     * The headset only stores 10 steps, so the read-back level is approximate:
     * setting 64 stores step 4, which reads back as 56.
     */
    static Result<SidetoneResult> parseSidetoneResponse(std::span<const uint8_t> params)
    {
        if (params.size() < 4 || params[3] > SIDETONE_DEVICE_MAX) {
            return DeviceError::protocolError("Unexpected G522 sidetone reply");
        }

        const uint8_t device_level = params[3];
        return SidetoneResult {
            .current_level = map<uint8_t>(device_level, 0, SIDETONE_DEVICE_MAX, 0, 128),
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = 0,
            .device_max    = SIDETONE_DEVICE_MAX,
            .is_muted      = device_level == 0,
            .device_level  = device_level,
        };
    }

    Result<LightsResult> setLights(hid_device* device_handle, bool on) override
    {
        // The headset has no separate on/off switch for its lighting: "off" writes a black
        // fixed color, and "on" restores G HUB's default look rather than the previous one.
        if (auto write_result = sendCenturionFeatureRequest(
                device_handle,
                static_cast<uint16_t>(protocols::CenturionFeature::HeadsetLighting),
                0x30,
                on ? LIGHTS_ON_PARAMS : LIGHTS_OFF_PARAMS);
            !write_result) {
            return write_result.error();
        }

        return LightsResult { .enabled = on };
    }

    Result<VoicePromptsResult> setVoicePrompts(hid_device* device_handle, bool enabled) override
    {
        // 1 = spoken prompts, 0 = tones (not silence). The difference is heard on the
        // Bluetooth/LIGHTSPEED switch ("lightspeed" spoken vs a beep); mic mute always beeps.
        if (auto write_result = sendCenturionFeatureRequest(
                device_handle,
                static_cast<uint16_t>(protocols::CenturionFeature::HeadsetVoicePrompts),
                0x50,
                std::array<uint8_t, 2> { 0x00, static_cast<uint8_t>(enabled) });
            !write_result) {
            return write_result.error();
        }

        return VoicePromptsResult { .enabled = enabled };
    }

    /**
     * @brief Build the auto-sleep write parameters: <sleep minutes> <lights dim> <lights off>.
     *
     * The same command also carries the two lighting inactivity timers (minutes, 0 = never),
     * and the headset rejects a write without them, so keep the values that were read back.
     */
    static Result<std::array<uint8_t, 3>> buildAutoSleepParams(uint8_t minutes, std::span<const uint8_t> current)
    {
        if (current.size() < 3) {
            return DeviceError::protocolError("Unexpected G522 auto-sleep reply");
        }
        return std::array<uint8_t, 3> { minutes, current[1], current[2] };
    }

    Result<InactiveTimeResult> setInactiveTime(hid_device* device_handle, uint8_t minutes) override
    {
        auto current = sendCenturionFeatureRequest(
            device_handle,
            static_cast<uint16_t>(protocols::CenturionFeature::CenturionAutoSleep),
            0x00);
        if (!current) {
            return current.error();
        }

        auto params = buildAutoSleepParams(minutes, *current);
        if (!params) {
            return params.error();
        }

        if (auto write_result = sendCenturionFeatureRequest(
                device_handle,
                static_cast<uint16_t>(protocols::CenturionFeature::CenturionAutoSleep),
                0x10,
                *params);
            !write_result) {
            return write_result.error();
        }

        return InactiveTimeResult {
            .minutes     = minutes,
            .min_minutes = 0,
            .max_minutes = 90
        };
    }

    Result<MicMuteLedBrightnessResult> setMicMuteLedBrightness(hid_device* device_handle, uint8_t brightness) override
    {
        uint8_t mute_led = static_cast<uint8_t>(static_cast<bool>(brightness)); // 0 or 1

        if (auto write_result = sendCenturionFeatureRequest(
                device_handle,
                static_cast<uint16_t>(protocols::CenturionFeature::HeadsetMicMuteLed),
                0x20,
                std::array<uint8_t, 1> { mute_led });
            !write_result) {
            return write_result.error();
        }

        return MicMuteLedBrightnessResult {
            .brightness     = mute_led,
            .min_brightness = 0,
            .max_brightness = 1
        };
    }

    Result<EqualizerPresetResult> setEqualizerPreset(hid_device* device_handle, uint8_t preset) override
    {
        if (preset >= EQ_PRESETS_COUNT) {
            return DeviceError::invalidParameter("Device only supports presets 0-4");
        }

        if (auto write_result = writeEqualizer(device_handle, *EQ_PRESETS[preset]); !write_result) {
            return write_result.error();
        }

        return EqualizerPresetResult {
            .preset        = preset,
            .total_presets = EQ_PRESETS_COUNT
        };
    }

    Result<EqualizerResult> setEqualizer(hid_device* device_handle, const EqualizerSettings& settings) override
    {
        if (settings.size() != static_cast<int>(EQ_BANDS)) {
            return DeviceError::invalidParameter("Equalizer requires 10 gain values");
        }

        // Custom gains use the Default preset's frequencies and Q.
        EqBands bands = EQ_PRESET_DEFAULT;
        for (size_t i = 0; i < EQ_BANDS; ++i) {
            bands[i].gain_db = settings.bands[i];
            if (auto error = validateEqBand(bands[i])) {
                return *error;
            }
        }

        if (auto write_result = writeEqualizer(device_handle, bands); !write_result) {
            return write_result.error();
        }

        return EqualizerResult {};
    }

    Result<ParametricEqualizerResult> setParametricEqualizer(
        hid_device* device_handle,
        const ParametricEqualizerSettings& settings) override
    {
        if (settings.size() > static_cast<int>(EQ_BANDS)) {
            return DeviceError::invalidParameter("Device only supports up to 10 equalizer bands");
        }

        // Bands that are not given keep the Default preset, so "reset" restores it.
        EqBands bands = EQ_PRESET_DEFAULT;
        for (size_t i = 0; i < settings.bands.size(); ++i) {
            const auto& band = settings.bands[i];
            if (band.type != EqualizerFilterType::Peaking) {
                return DeviceError::invalidParameter("This headset only supports peaking EQ bands");
            }
            if (!std::isfinite(band.frequency) || band.frequency < EQ_FREQ_MIN || band.frequency > EQ_FREQ_MAX) {
                return DeviceError::invalidParameter("Frequency must be between 20 Hz and 20000 Hz");
            }

            bands[i] = EqBand {
                .frequency = static_cast<uint16_t>(std::lround(band.frequency)),
                .gain_db   = band.gain,
                .q_factor  = band.q_factor,
            };
            if (auto error = validateEqBand(bands[i])) {
                return *error;
            }
        }

        if (auto write_result = writeEqualizer(device_handle, bands); !write_result) {
            return write_result.error();
        }

        return ParametricEqualizerResult {};
    }

    /**
     * @brief Encode the equalizer parameters: 00 00 00, then 10 bands of 5 bytes.
     *
     * Gains are rounded to the headset's 0.05 dB steps and Q to steps of 1/32;
     * out-of-range values are clamped.
     */
    static std::vector<uint8_t> buildEqParams(std::span<const EqBand, EQ_BANDS> bands)
    {
        std::vector<uint8_t> params { 0x00, 0x00, 0x00 };
        params.reserve(EQ_PARAMS_SIZE);
        for (const auto& band : bands) {
            const auto q_raw    = std::clamp<long>(std::lround(band.q_factor * EQ_Q_SCALE), 1, 255);
            const auto gain_raw = std::clamp<long>(std::lround(band.gain_db / EQ_GAIN_STEP) + EQ_GAIN_ZERO_RAW,
                0, 2 * EQ_GAIN_ZERO_RAW);
            params.push_back(static_cast<uint8_t>(band.frequency >> 8));
            params.push_back(static_cast<uint8_t>(band.frequency & 0xFF));
            params.push_back(static_cast<uint8_t>(q_raw));
            params.push_back(static_cast<uint8_t>(gain_raw >> 8));
            params.push_back(static_cast<uint8_t>(gain_raw & 0xFF));
        }
        return params;
    }

protected:
    std::span<const uint8_t> centurionFramePrefix() const override
    {
        return FRAME_PREFIX;
    }

    protocols::CenturionOptions centurionOptions() const override
    {
        return {
            .exact_direct_length      = true,
            .detect_connection_events = true,
            .probe_offline_on_timeout = true,
            .lookup_features_by_id    = true,
        };
    }

private:
    static std::optional<DeviceError> validateEqBand(const EqBand& band)
    {
        if (!std::isfinite(band.gain_db) || band.gain_db < EQ_GAIN_MIN || band.gain_db > EQ_GAIN_MAX) {
            return DeviceError::invalidParameter("Gain must be between -6 and +6 dB");
        }
        // Check Q after rounding to the headset's 1/32 steps, so G HUB's displayed limits
        // (0.031 and 7.969) are accepted.
        if (!std::isfinite(band.q_factor) || std::lround(band.q_factor * EQ_Q_SCALE) < 1
            || std::lround(band.q_factor * EQ_Q_SCALE) > 255) {
            return DeviceError::invalidParameter("Q factor must be between 0.031 and 7.969");
        }
        return std::nullopt;
    }

    Result<void> writeEqualizer(hid_device* device_handle, const EqBands& bands) const
    {
        auto write_result = sendCenturionFeatureRequest(
            device_handle,
            static_cast<uint16_t>(protocols::CenturionFeature::HeadsetAdvancedParaEQ),
            0x20,
            buildEqParams(bands));
        if (!write_result) {
            return write_result.error();
        }
        return {};
    }
};

} // namespace headsetcontrol
