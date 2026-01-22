#pragma once

#include "../result_types.hpp"
#include "device_utils.hpp"
#include "protocols/steelseries_protocol.hpp"
#include <array>
#include <string_view>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief SteelSeries Arctis Nova 5/5X Gaming Headset
 *
 * Features:
 * - Sidetone (10 levels)
 * - Battery status
 * - Chatmix
 * - Microphone mute LED brightness
 * - Microphone volume (16 levels)
 * - Inactive time
 * - Volume limiter
 * - Equalizer (10 bands)
 * - Parametric equalizer (ADVANCED!)
 */
class SteelSeriesArctisNova5 : public protocols::SteelSeriesNovaDevice<SteelSeriesArctisNova5> {
public:
    static constexpr std::array<uint16_t, 2> SUPPORTED_PRODUCT_IDS {
        0x2232, // Nova 5 Base Station
        0x2253 // Nova 5X Base Station
    };

    static constexpr int EQUALIZER_BANDS          = 10;
    static constexpr float EQUALIZER_GAIN_MIN     = -10.0f;
    static constexpr float EQUALIZER_GAIN_MAX     = 10.0f;
    static constexpr float EQUALIZER_GAIN_STEP    = 0.5f;
    static constexpr uint8_t EQUALIZER_GAIN_BASE  = 20;
    static constexpr int EQUALIZER_PRESETS_COUNT  = 4;
    static constexpr float EQUALIZER_Q_FACTOR_MIN = 0.2f;
    static constexpr float EQUALIZER_Q_FACTOR_MAX = 10.0f;
    static constexpr float EQUALIZER_Q_DEFAULT    = 1.414f;
    static constexpr int EQUALIZER_FREQ_MIN       = 20;
    static constexpr int EQUALIZER_FREQ_MAX       = 20000;
    static constexpr int EQUALIZER_FREQ_DISABLED  = 20001;

    // EQ band frequencies used in default presets (little-endian)
    static constexpr std::array<uint16_t, EQUALIZER_BANDS> BAND_FREQUENCIES {
        32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000
    };

    // Preset arrays (flat, bass, focus, smiley)
    static constexpr std::array<float, EQUALIZER_BANDS> PRESET_FLAT { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    static constexpr std::array<float, EQUALIZER_BANDS> PRESET_BASS { 3.5f, 5.5f, 4, 1, -1.5f, -1.5f, -1, -1, -1, -1 };
    static constexpr std::array<float, EQUALIZER_BANDS> PRESET_FOCUS { -5, -3.5f, -1, -3.5f, -2.5f, 4, 6, -3.5f, 0, 0 };
    static constexpr std::array<float, EQUALIZER_BANDS> PRESET_SMILEY { 3, 3.5f, 1.5f, -1.5f, -4, -4, -2.5f, 1.5f, 3, 4 };

    // Filter type mapping: C++ enum -> device byte
    static constexpr uint8_t getFilterByte(EqualizerFilterType type)
    {
        switch (type) {
        case EqualizerFilterType::Peaking:
            return 1;
        case EqualizerFilterType::LowPass:
            return 2;
        case EqualizerFilterType::HighPass:
            return 3;
        case EqualizerFilterType::LowShelf:
            return 4;
        case EqualizerFilterType::HighShelf:
            return 5;
        default:
            return 1; // Default to peaking
        }
    }

    std::vector<uint16_t> getProductIds() const override
    {
        return { SUPPORTED_PRODUCT_IDS.begin(), SUPPORTED_PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "SteelSeries Arctis Nova (5/5X)"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_CHATMIX_STATUS)
            | B(CAP_MICROPHONE_MUTE_LED_BRIGHTNESS) | B(CAP_MICROPHONE_VOLUME)
            | B(CAP_INACTIVE_TIME) | B(CAP_VOLUME_LIMITER) | B(CAP_EQUALIZER_PRESET)
            | B(CAP_EQUALIZER) | B(CAP_PARAMETRIC_EQUALIZER);
    }

    constexpr capability_detail getCapabilityDetail([[maybe_unused]] enum capabilities cap) const override
    {
        // All capabilities use interface 3
        return { .usagepage = 0xffc0, .usageid = 0x1, .interface_id = 3 };
    }

    std::optional<EqualizerInfo> getEqualizerInfo() const override
    {
        return EqualizerInfo {
            .bands_count    = EQUALIZER_BANDS,
            .bands_baseline = 0,
            .bands_step     = EQUALIZER_GAIN_STEP,
            .bands_min      = static_cast<int>(EQUALIZER_GAIN_MIN),
            .bands_max      = static_cast<int>(EQUALIZER_GAIN_MAX)
        };
    }

    uint8_t getEqualizerPresetsCount() const override
    {
        return EQUALIZER_PRESETS_COUNT;
    }

    std::optional<EqualizerPresets> getEqualizerPresets() const override
    {
        EqualizerPresets presets;
        presets.presets = {
            { "Flat", std::vector<float>(PRESET_FLAT.begin(), PRESET_FLAT.end()) },
            { "Bass", std::vector<float>(PRESET_BASS.begin(), PRESET_BASS.end()) },
            { "Focus", std::vector<float>(PRESET_FOCUS.begin(), PRESET_FOCUS.end()) },
            { "Smiley", std::vector<float>(PRESET_SMILEY.begin(), PRESET_SMILEY.end()) }
        };
        return presets;
    }

    std::optional<ParametricEqualizerInfo> getParametricEqualizerInfo() const override
    {
        // Supported filters: LowShelf, LowPass, Peaking, HighPass, HighShelf
        int supported_filters = B(static_cast<int>(EqualizerFilterType::LowShelf))
            | B(static_cast<int>(EqualizerFilterType::LowPass))
            | B(static_cast<int>(EqualizerFilterType::Peaking))
            | B(static_cast<int>(EqualizerFilterType::HighPass))
            | B(static_cast<int>(EqualizerFilterType::HighShelf));

        return ParametricEqualizerInfo {
            .bands_count    = EQUALIZER_BANDS,
            .gain_base      = static_cast<float>(EQUALIZER_GAIN_BASE),
            .gain_step      = EQUALIZER_GAIN_STEP,
            .gain_min       = EQUALIZER_GAIN_MIN,
            .gain_max       = EQUALIZER_GAIN_MAX,
            .q_factor_min   = EQUALIZER_Q_FACTOR_MIN,
            .q_factor_max   = EQUALIZER_Q_FACTOR_MAX,
            .freq_min       = EQUALIZER_FREQ_MIN,
            .freq_max       = EQUALIZER_FREQ_MAX,
            .filter_types   = supported_filters
        };
    }

    // Helper: save state (2 commands)
    Result<void> saveStateNova5(hid_device* device_handle) const
    {
        std::array<uint8_t, 2> save1 { 0x0, 0x09 };
        auto result1 = writeHID(device_handle, save1, MSG_SIZE);
        if (!result1) {
            return result1.error();
        }

        std::array<uint8_t, 3> save2 { 0x0, 0x35, 0x01 };
        auto result2 = writeHID(device_handle, save2, MSG_SIZE);
        if (!result2) {
            return result2.error();
        }

        return {};
    }

    // Rich Results V2 API
    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        auto status_result = readDeviceStatus(device_handle);
        if (!status_result) {
            return status_result.error();
        }

        auto& data = *status_result;
        if (data.size() < 16) {
            return DeviceError::protocolError("Response too short");
        }

        // Check if headset is online
        constexpr uint8_t HEADSET_OFFLINE  = 0x02;
        constexpr uint8_t HEADSET_CHARGING = 0x01;

        if (data[1] == HEADSET_OFFLINE) {
            return DeviceError::deviceOffline("Headset not connected");
        }

        enum battery_status status = BATTERY_AVAILABLE;
        if (data[4] == HEADSET_CHARGING) {
            status = BATTERY_CHARGING;
        }

        int level = data[3]; // Already in percentage

        return BatteryResult {
            .level_percent = level,
            .status        = status,
            .raw_data      = data
        };
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        // Nova 5 supports 11 levels (0x00-0x0a)
        uint8_t mapped = mapSidetoneToDiscrete<11>(level);

        std::array<uint8_t, 3> cmd { 0x0, 0x39, mapped };
        if (auto result = writeHID(device_handle, cmd, MSG_SIZE); !result) {
            return result.error();
        }

        if (auto result = saveStateNova5(device_handle); !result) {
            return result.error();
        }

        return SidetoneResult {
            .current_level = level,
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = 0x00,
            .device_max    = 0x0a
        };
    }

    Result<InactiveTimeResult> setInactiveTime(hid_device* device_handle, uint8_t minutes) override
    {
        std::array<uint8_t, 3> cmd { 0x0, 0xA3, minutes };

        auto result = writeHID(device_handle, cmd, MSG_SIZE);
        if (!result) {
            return result.error();
        }

        return InactiveTimeResult {
            .minutes     = minutes,
            .min_minutes = 0,
            .max_minutes = 255
        };
    }

    Result<MicVolumeResult> setMicVolume(hid_device* device_handle, uint8_t volume) override
    {
        // 16 levels: 0x00 to 0x0F
        uint8_t mapped = volume / 8;
        if (mapped == 16)
            mapped = 15;

        std::array<uint8_t, 3> cmd { 0x0, 0x37, mapped };
        auto write_result = writeHID(device_handle, cmd, MSG_SIZE);
        if (!write_result) {
            return write_result.error();
        }

        auto save_result = saveStateNova5(device_handle);
        if (!save_result) {
            return save_result.error();
        }

        return MicVolumeResult {
            .volume     = volume,
            .min_volume = 0,
            .max_volume = 128
        };
    }

    Result<MicMuteLedBrightnessResult> setMicMuteLedBrightness(hid_device* device_handle, uint8_t brightness) override
    {
        // Mapping: 0=off, 1=low, 2=medium (0x04), 3=high (0x0a)
        uint8_t mapped = brightness;
        if (brightness == 2) {
            mapped = 0x04;
        } else if (brightness == 3) {
            mapped = 0x0A;
        }

        std::array<uint8_t, 3> cmd { 0x0, 0xAE, mapped };
        auto write_result = writeHID(device_handle, cmd, MSG_SIZE);
        if (!write_result) {
            return write_result.error();
        }

        auto save_result = saveStateNova5(device_handle);
        if (!save_result) {
            return save_result.error();
        }

        return MicMuteLedBrightnessResult {
            .brightness     = brightness,
            .min_brightness = 0,
            .max_brightness = 3
        };
    }

    Result<VolumeLimiterResult> setVolumeLimiter(hid_device* device_handle, bool enabled) override
    {
        std::array<uint8_t, 3> cmd { 0x0, 0x27, static_cast<uint8_t>(enabled ? 1 : 0) };
        auto write_result = writeHID(device_handle, cmd, MSG_SIZE);
        if (!write_result) {
            return write_result.error();
        }

        auto save_result = saveStateNova5(device_handle);
        if (!save_result) {
            return save_result.error();
        }

        return VolumeLimiterResult { .enabled = enabled };
    }

    Result<ChatmixResult> getChatmix(hid_device* device_handle) override
    {
        auto status_result = readDeviceStatus(device_handle);
        if (!status_result) {
            return status_result.error();
        }

        auto& data = *status_result;
        if (data.size() < 7) {
            return DeviceError::protocolError("Response too short for chatmix");
        }

        // Note: Don't check for offline here - the dongle always reports valid
        // chatmix data even when the headset is disconnected.

        // Chatmix data: game volume at byte 5, chat volume at byte 6
        // Values range from 0 to 100
        int game_raw = data[5];
        int chat_raw = data[6];

        // Map to normalized chatmix value (0-128, 64 = center)
        int game = map(game_raw, 0, 100, 0, 64);
        int chat = map(chat_raw, 0, 100, 0, -64);
        int level = 64 - (chat + game);

        // Calculate percentages
        int game_pct = game_raw;
        int chat_pct = chat_raw;

        return ChatmixResult {
            .level               = level,
            .game_volume_percent = game_pct,
            .chat_volume_percent = chat_pct
        };
    }

    Result<EqualizerPresetResult> setEqualizerPreset(hid_device* device_handle, uint8_t preset) override
    {
        const std::array<float, EQUALIZER_BANDS>* preset_values;
        switch (preset) {
        case 0:
            preset_values = &PRESET_FLAT;
            break;
        case 1:
            preset_values = &PRESET_BASS;
            break;
        case 2:
            preset_values = &PRESET_FOCUS;
            break;
        case 3:
            preset_values = &PRESET_SMILEY;
            break;
        default:
            return DeviceError::invalidParameter("Device only supports presets 0-3");
        }

        // Build equalizer settings from preset
        EqualizerSettings settings;
        settings.bands.assign(preset_values->begin(), preset_values->end());

        auto result = setEqualizer(device_handle, settings);
        if (!result) {
            return result.error();
        }

        return EqualizerPresetResult { .preset = preset, .total_presets = EQUALIZER_PRESETS_COUNT };
    }

    Result<EqualizerResult> setEqualizer(hid_device* device_handle, const EqualizerSettings& settings) override
    {
        if (settings.size() != EQUALIZER_BANDS) {
            return DeviceError::invalidParameter("Device requires exactly 10 equalizer bands");
        }

        std::array<uint8_t, MSG_SIZE> data {};
        data[0] = 0x0;
        data[1] = 0x33;

        for (int i = 0; i < EQUALIZER_BANDS; i++) {
            float gain_value = settings.bands[i];
            if (gain_value < EQUALIZER_GAIN_MIN || gain_value > EQUALIZER_GAIN_MAX) {
                return DeviceError::invalidParameter("Gain values must be between -10 and +10");
            }

            uint8_t raw_gain = static_cast<uint8_t>(EQUALIZER_GAIN_BASE + gain_value * 2);

            // Gain flag: 0x01 for baseline, 0x04 for first band with non-baseline gain,
            // 0x05 for last band with non-baseline gain
            uint8_t gain_flag = 0x01;
            if (raw_gain != EQUALIZER_GAIN_BASE) {
                if (i == 0) {
                    gain_flag = 0x04;
                } else if (i == EQUALIZER_BANDS - 1) {
                    gain_flag = 0x05;
                }
            }

            // Each band: 2 bytes frequency (LE), 1 byte gain flag, 1 byte gain, 2 bytes Q-factor (LE)
            uint16_t freq    = BAND_FREQUENCIES[i];
            data[2 + 6 * i]     = freq & 0xFF; // Frequency low byte
            data[2 + 6 * i + 1] = (freq >> 8) & 0xFF; // Frequency high byte
            data[2 + 6 * i + 2] = gain_flag;
            data[2 + 6 * i + 3] = raw_gain;
            // Default Q-factor: 1.414 * 1000 = 1414 = 0x0586
            data[2 + 6 * i + 4] = 0x86;
            data[2 + 6 * i + 5] = 0x05;
        }

        auto write_result = writeHID(device_handle, data, MSG_SIZE);
        if (!write_result) {
            return write_result.error();
        }

        auto save_result = saveStateNova5(device_handle);
        if (!save_result) {
            return save_result.error();
        }

        return EqualizerResult {};
    }

    Result<ParametricEqualizerResult> setParametricEqualizer(hid_device* device_handle, const ParametricEqualizerSettings& settings) override
    {
        if (settings.size() > EQUALIZER_BANDS) {
            return DeviceError::invalidParameter("Device only supports up to 10 equalizer bands");
        }

        std::array<uint8_t, MSG_SIZE> data {};
        data[0] = 0x0;
        data[1] = 0x33;

        // Write provided bands
        for (int i = 0; i < settings.size(); i++) {
            const auto& band = settings.bands[i];

            // Validate frequency
            if (band.frequency != EQUALIZER_FREQ_DISABLED) {
                if (band.frequency < EQUALIZER_FREQ_MIN || band.frequency > EQUALIZER_FREQ_MAX) {
                    return DeviceError::invalidParameter("Frequency must be between 20 and 20000 Hz");
                }
            }

            // Validate Q-factor
            if (band.q_factor < EQUALIZER_Q_FACTOR_MIN || band.q_factor > EQUALIZER_Q_FACTOR_MAX) {
                return DeviceError::invalidParameter("Q-factor must be between 0.2 and 10.0");
            }

            // Validate gain
            if (band.gain < EQUALIZER_GAIN_MIN || band.gain > EQUALIZER_GAIN_MAX) {
                return DeviceError::invalidParameter("Gain must be between -10 and +10");
            }

            // Write frequency (LE)
            uint16_t freq = static_cast<uint16_t>(band.frequency);
            data[2 + 6 * i]     = freq & 0xFF;
            data[2 + 6 * i + 1] = (freq >> 8) & 0xFF;

            // Write filter type
            data[2 + 6 * i + 2] = getFilterByte(band.type);

            // Write gain
            data[2 + 6 * i + 3] = static_cast<uint8_t>((band.gain + 10.0f) * 2.0f);

            // Write Q-factor (LE)
            uint16_t q = static_cast<uint16_t>(band.q_factor * 1000);
            data[2 + 6 * i + 4] = q & 0xFF;
            data[2 + 6 * i + 5] = (q >> 8) & 0xFF;
        }

        // Fill remaining bands with disabled band
        for (int i = settings.size(); i < EQUALIZER_BANDS; i++) {
            // Disabled frequency
            uint16_t freq = EQUALIZER_FREQ_DISABLED;
            data[2 + 6 * i]     = freq & 0xFF;
            data[2 + 6 * i + 1] = (freq >> 8) & 0xFF;

            // Peaking filter type
            data[2 + 6 * i + 2] = getFilterByte(EqualizerFilterType::Peaking);

            // Gain = 0 -> raw value = 20
            data[2 + 6 * i + 3] = EQUALIZER_GAIN_BASE;

            // Default Q-factor: 1.414 * 1000 = 1414 = 0x0586
            data[2 + 6 * i + 4] = 0x86;
            data[2 + 6 * i + 5] = 0x05;
        }

        auto write_result = writeHID(device_handle, data, MSG_SIZE);
        if (!write_result) {
            return write_result.error();
        }

        auto save_result = saveStateNova5(device_handle);
        if (!save_result) {
            return save_result.error();
        }

        return ParametricEqualizerResult {};
    }
};
} // namespace headsetcontrol
