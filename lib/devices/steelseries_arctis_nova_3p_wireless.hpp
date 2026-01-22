#pragma once

#include "../result_types.hpp"
#include "protocols/steelseries_protocol.hpp"
#include <array>
#include <string_view>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief SteelSeries Arctis Nova 3P/3X Wireless Gaming Headset
 *
 * Features:
 * - Sidetone (10 levels)
 * - Microphone volume (14 levels)
 * - Inactive time
 * - Battery status
 * - Equalizer (10 bands)
 * - Parametric equalizer (ADVANCED!)
 *
 * NOTE: Windows support has HID packet length issues
 */
class SteelSeriesArctisNova3PWireless : public protocols::SteelSeriesNovaDevice<SteelSeriesArctisNova3PWireless> {
public:
    static constexpr std::array<uint16_t, 2> SUPPORTED_PRODUCT_IDS {
        0x2269, // Arctis Nova 3P Wireless
        0x226d // Arctis Nova 3X Wireless
    };

    static constexpr int EQUALIZER_BANDS          = 10;
    static constexpr float EQUALIZER_GAIN_MIN     = -12.0f;
    static constexpr float EQUALIZER_GAIN_MAX     = 12.0f;
    static constexpr float EQUALIZER_GAIN_STEP    = 0.1f;
    static constexpr int EQUALIZER_PRESETS_COUNT  = 4;
    static constexpr float EQUALIZER_Q_FACTOR_MIN = 0.2f;
    static constexpr float EQUALIZER_Q_FACTOR_MAX = 10.0f;
    static constexpr float EQUALIZER_Q_DEFAULT    = 1.414f;
    static constexpr int EQUALIZER_FREQ_MIN       = 20;
    static constexpr int EQUALIZER_FREQ_MAX       = 20000;
    static constexpr int EQUALIZER_FREQ_DISABLED  = 20001;

    // EQ band frequencies used in default presets (32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000)
    static constexpr std::array<uint16_t, EQUALIZER_BANDS> BAND_FREQUENCIES {
        32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000
    };

    // Preset arrays
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
        case EqualizerFilterType::Notch:
            return 6;
        default:
            return 1; // Default to peaking
        }
    }

    // Encode gain value (two's complement for negative)
    static uint8_t encodeGain(float gain)
    {
        if (gain < 0) {
            int raw = static_cast<int>((-gain) / EQUALIZER_GAIN_STEP);
            return static_cast<uint8_t>((0xff - raw) + 1);
        } else {
            return static_cast<uint8_t>(gain / EQUALIZER_GAIN_STEP);
        }
    }

    std::vector<uint16_t> getProductIds() const override
    {
        return { SUPPORTED_PRODUCT_IDS.begin(), SUPPORTED_PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "SteelSeries Arctis Nova 3P Wireless"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_SIDETONE) | B(CAP_INACTIVE_TIME) | B(CAP_MICROPHONE_VOLUME)
            | B(CAP_BATTERY_STATUS) | B(CAP_EQUALIZER) | B(CAP_EQUALIZER_PRESET)
            | B(CAP_PARAMETRIC_EQUALIZER);
    }

    constexpr capability_detail getCapabilityDetail([[maybe_unused]] enum capabilities cap) const override
    {
        return { .usagepage = 0xffc0, .usageid = 0x1, .interface_id = 0 };
    }

    constexpr uint8_t getSupportedPlatforms() const override
    {
        // Windows has HID packet length issues
        return PLATFORM_LINUX | PLATFORM_MACOS;
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
        // Supported filters: LowShelf, LowPass, Peaking, HighPass, HighShelf, Notch
        int supported_filters = B(static_cast<int>(EqualizerFilterType::LowShelf))
            | B(static_cast<int>(EqualizerFilterType::LowPass))
            | B(static_cast<int>(EqualizerFilterType::Peaking))
            | B(static_cast<int>(EqualizerFilterType::HighPass))
            | B(static_cast<int>(EqualizerFilterType::HighShelf))
            | B(static_cast<int>(EqualizerFilterType::Notch));

        return ParametricEqualizerInfo {
            .bands_count  = EQUALIZER_BANDS,
            .gain_base    = 0.0f,
            .gain_step    = EQUALIZER_GAIN_STEP,
            .gain_min     = EQUALIZER_GAIN_MIN,
            .gain_max     = EQUALIZER_GAIN_MAX,
            .q_factor_min = EQUALIZER_Q_FACTOR_MIN,
            .q_factor_max = EQUALIZER_Q_FACTOR_MAX,
            .freq_min     = EQUALIZER_FREQ_MIN,
            .freq_max     = EQUALIZER_FREQ_MAX,
            .filter_types = supported_filters
        };
    }

    // Helper: save state
    Result<void> saveStateNova3P(hid_device* device_handle) const
    {
        std::array<uint8_t, 1> cmd { 0x09 };
        return sendFeatureReport(device_handle, cmd, MSG_SIZE);
    }

    // Rich Results V2 API
    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        std::array<uint8_t, 1> request { 0xb0 };
        if (auto result = sendFeatureReport(device_handle, request, MSG_SIZE); !result) {
            return result.error();
        }

        std::array<uint8_t, 4> response {};
        auto read_result = readHIDTimeout(device_handle, response, hsc_device_timeout);

        if (!read_result) {
            return read_result.error();
        }

        constexpr uint8_t HEADSET_OFFLINE = 0x02;
        if (response[1] == HEADSET_OFFLINE) {
            return DeviceError::deviceOffline("Headset not connected");
        }

        int level = map(response[3], 0, 100, 0, 100);

        return BatteryResult {
            .level_percent = level,
            .status        = BATTERY_AVAILABLE,
            .raw_data      = std::vector<uint8_t> { response.begin(), response.end() }
        };
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        uint8_t mapped = map<uint8_t>(level, 0, 128, 0, 0xa);

        std::array<uint8_t, 2> cmd { 0x39, mapped };
        if (auto result = sendFeatureReport(device_handle, cmd, MSG_SIZE); !result) {
            return result.error();
        }

        if (auto save_result = saveStateNova3P(device_handle); !save_result) {
            return save_result.error();
        }

        return SidetoneResult {
            .current_level = level,
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = 0,
            .device_max    = 0xa
        };
    }

    Result<InactiveTimeResult> setInactiveTime(hid_device* device_handle, uint8_t minutes) override
    {
        // Round to supported values
        if (minutes >= 90)
            minutes = 90;
        else if (minutes >= 75)
            minutes = 75;
        else if (minutes >= 60)
            minutes = 60;
        else if (minutes >= 45)
            minutes = 45;
        else if (minutes >= 30)
            minutes = 30;
        else if (minutes >= 15)
            minutes = 15;
        else if (minutes >= 10)
            minutes = 10;
        else if (minutes >= 5)
            minutes = 5;
        else if (minutes >= 1)
            minutes = 1;
        else
            minutes = 0; // Never turn off

        std::array<uint8_t, 2> cmd { 0xa3, minutes };
        if (auto result = sendFeatureReport(device_handle, cmd, MSG_SIZE); !result) {
            return result.error();
        }

        if (auto save_result = saveStateNova3P(device_handle); !save_result) {
            return save_result.error();
        }

        return InactiveTimeResult {
            .minutes     = minutes,
            .min_minutes = 0,
            .max_minutes = 90
        };
    }

    Result<MicVolumeResult> setMicVolume(hid_device* device_handle, uint8_t volume) override
    {
        uint8_t mapped = map<uint8_t>(volume, 0, 128, 0, 0x0e);

        std::array<uint8_t, 2> cmd { 0x37, mapped };
        if (auto result = sendFeatureReport(device_handle, cmd, MSG_SIZE); !result) {
            return result.error();
        }

        if (auto save_result = saveStateNova3P(device_handle); !save_result) {
            return save_result.error();
        }

        return MicVolumeResult {
            .volume     = volume,
            .min_volume = 0,
            .max_volume = 128
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
        data[0] = 0x33;

        for (int i = 0; i < EQUALIZER_BANDS; i++) {
            float gain_value = settings.bands[i];
            if (gain_value < EQUALIZER_GAIN_MIN || gain_value > EQUALIZER_GAIN_MAX) {
                return DeviceError::invalidParameter("Gain values must be between -12 and +12");
            }

            // Each band: 2 bytes frequency (LE), 1 byte filter type, 1 byte gain, 2 bytes Q-factor (LE)
            uint16_t freq       = BAND_FREQUENCIES[i];
            data[1 + 6 * i]     = freq & 0xFF;
            data[1 + 6 * i + 1] = (freq >> 8) & 0xFF;
            data[1 + 6 * i + 2] = 0x01; // Peaking filter
            data[1 + 6 * i + 3] = encodeGain(gain_value);
            // Default Q-factor: 1.414 * 1000 = 1414 = 0x0586
            data[1 + 6 * i + 4] = 0x86;
            data[1 + 6 * i + 5] = 0x05;
        }

        if (auto result = sendFeatureReport(device_handle, data, MSG_SIZE); !result) {
            return result.error();
        }

        if (auto save_result = saveStateNova3P(device_handle); !save_result) {
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
        data[0] = 0x33;

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
                return DeviceError::invalidParameter("Gain must be between -12 and +12");
            }

            // Write frequency (LE)
            uint16_t freq       = static_cast<uint16_t>(band.frequency);
            data[1 + 6 * i]     = freq & 0xFF;
            data[1 + 6 * i + 1] = (freq >> 8) & 0xFF;

            // Write filter type
            data[1 + 6 * i + 2] = getFilterByte(band.type);

            // Write gain (two's complement for negative)
            data[1 + 6 * i + 3] = encodeGain(band.gain);

            // Write Q-factor (LE)
            uint16_t q          = static_cast<uint16_t>(band.q_factor * 1000);
            data[1 + 6 * i + 4] = q & 0xFF;
            data[1 + 6 * i + 5] = (q >> 8) & 0xFF;
        }

        // Fill remaining bands with disabled band
        for (int i = settings.size(); i < EQUALIZER_BANDS; i++) {
            // Disabled frequency
            uint16_t freq       = EQUALIZER_FREQ_DISABLED;
            data[1 + 6 * i]     = freq & 0xFF;
            data[1 + 6 * i + 1] = (freq >> 8) & 0xFF;

            // Peaking filter type
            data[1 + 6 * i + 2] = getFilterByte(EqualizerFilterType::Peaking);

            // Gain = 0
            data[1 + 6 * i + 3] = 0;

            // Default Q-factor: 1.414 * 1000 = 1414 = 0x0586
            data[1 + 6 * i + 4] = 0x86;
            data[1 + 6 * i + 5] = 0x05;
        }

        if (auto result = sendFeatureReport(device_handle, data, MSG_SIZE); !result) {
            return result.error();
        }

        if (auto save_result = saveStateNova3P(device_handle); !save_result) {
            return save_result.error();
        }

        return ParametricEqualizerResult {};
    }
};
} // namespace headsetcontrol
