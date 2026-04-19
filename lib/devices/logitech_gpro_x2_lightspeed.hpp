#pragma once

#include "../utility.hpp"
#include "protocols/logitech_centurion_protocol.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Logitech G PRO X 2 LIGHTSPEED (PID 0x0af7)
 *
 * This variant uses a vendor-specific 64-byte protocol on usage page 0xffa0
 * for battery data instead of HID++.
 */
class LogitechGProX2Lightspeed : public protocols::LogitechCenturionProtocol {
public:
    static constexpr std::array<uint16_t, 1> SUPPORTED_PRODUCT_IDS { 0x0af7 };
    static constexpr size_t PACKET_SIZE                                        = 64;
    static constexpr uint8_t REPORT_PREFIX                                     = 0x51;
    static constexpr uint8_t SIDETONE_DEVICE_MAX                               = 100;
    static constexpr uint8_t SIDETONE_MIC_ID                                   = 0x01;
    static constexpr uint8_t PLAYBACK_DIRECTION                                = 0x00;
    static constexpr uint8_t EQUALIZER_PRESETS_COUNT                           = 5;

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
        return "Logitech G PRO X 2 LIGHTSPEED"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS) | B(CAP_INACTIVE_TIME)
            | B(CAP_EQUALIZER_PRESET)
            | B(CAP_EQUALIZER) | B(CAP_PARAMETRIC_EQUALIZER);
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
            { "Bass Boost", std::vector<float>(PRESET_BASS_BOOST.begin(), PRESET_BASS_BOOST.end()) },
            { "Team Chat", std::vector<float>(PRESET_TEAM_CHAT.begin(), PRESET_TEAM_CHAT.end()) },
            { "Shooter", std::vector<float>(PRESET_SHOOTER.begin(), PRESET_SHOOTER.end()) },
            { "MOBA", std::vector<float>(PRESET_MOBA.begin(), PRESET_MOBA.end()) },
        };
        return presets;
    }

    std::optional<EqualizerInfo> getEqualizerInfo() const override
    {
        if (!has_cached_equalizer_info_) {
            return std::nullopt;
        }

        return EqualizerInfo {
            .bands_count    = cached_band_count_,
            .bands_baseline = 0,
            .bands_step     = 1.0f,
            .bands_min      = cached_gain_min_,
            .bands_max      = cached_gain_max_
        };
    }

    std::optional<ParametricEqualizerInfo> getParametricEqualizerInfo() const override
    {
        if (!has_cached_equalizer_info_) {
            return std::nullopt;
        }

        return ParametricEqualizerInfo {
            .bands_count  = cached_band_count_,
            .gain_base    = 0.0f,
            .gain_step    = 1.0f,
            .gain_min     = static_cast<float>(cached_gain_min_),
            .gain_max     = static_cast<float>(cached_gain_max_),
            .q_factor_min = 1.0f,
            .q_factor_max = 1.0f,
            .freq_min     = 20,
            .freq_max     = 20000,
            .filter_types = B(static_cast<int>(EqualizerFilterType::Peaking))
        };
    }

    constexpr capability_detail getCapabilityDetail(enum capabilities cap) const override
    {
        switch (cap) {
        case CAP_BATTERY_STATUS:
        case CAP_SIDETONE:
        case CAP_INACTIVE_TIME:
        case CAP_EQUALIZER_PRESET:
        case CAP_EQUALIZER:
        case CAP_PARAMETRIC_EQUALIZER:
            return { .usagepage = 0xffa0, .usageid = 0x0001, .interface_id = 3 };
        default:
            return HIDDevice::getCapabilityDetail(cap);
        }
    }

    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        auto centurion_start_time = std::chrono::steady_clock::now();
        if (auto centurion_battery = sendCenturionFeatureRequest(
                device_handle,
                static_cast<uint16_t>(protocols::CenturionFeature::CenturionBatterySoc),
                0x00);
            centurion_battery) {
            auto battery_result = parseCenturionBatteryResponse(*centurion_battery);
            if (!battery_result) {
                return battery_result.error();
            }

            battery_result->raw_data       = *centurion_battery;
            auto centurion_end_time        = std::chrono::steady_clock::now();
            battery_result->query_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                centurion_end_time - centurion_start_time);
            return *battery_result;
        }

        auto start_time = std::chrono::steady_clock::now();

        std::array<uint8_t, PACKET_SIZE> request = buildBatteryRequest();
        if (auto write_result = writeHID(device_handle, request, PACKET_SIZE); !write_result) {
            return write_result.error();
        }

        std::vector<uint8_t> raw_packets;
        raw_packets.reserve(PACKET_SIZE * 4);

        for (int attempt = 0; attempt < 4; ++attempt) {
            std::array<uint8_t, PACKET_SIZE> response {};
            if (auto read_result = readHIDTimeout(device_handle, response, hsc_device_timeout); !read_result) {
                return read_result.error();
            }

            raw_packets.insert(raw_packets.end(), response.begin(), response.end());

            if (isPowerOffPacket(response)) {
                return DeviceError::deviceOffline("Headset is powered off or not connected");
            }

            if (isPowerEventPacket(response)) {
                continue;
            }

            if (isAckPacket(response)) {
                continue;
            }

            if (!isBatteryResponsePacket(response)) {
                continue;
            }

            auto battery_result = parseBatteryResponse(response);
            if (!battery_result) {
                return battery_result.error();
            }

            battery_result->raw_data       = std::move(raw_packets);
            auto end_time                  = std::chrono::steady_clock::now();
            battery_result->query_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            return *battery_result;
        }

        return DeviceError::protocolError("Battery response packet not received");
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        uint8_t mapped = map<uint8_t>(level, 0, 128, 0, SIDETONE_DEVICE_MAX);

        auto sidetone_state = sendCenturionFeatureRequest(
            device_handle,
            static_cast<uint16_t>(protocols::CenturionFeature::HeadsetAudioSidetone),
            0x00);
        if (!sidetone_state) {
            return sidetone_state.error();
        }

        std::array<uint8_t, 3> version2_payload { SIDETONE_MIC_ID, 0xFF, mapped };
        std::array<uint8_t, 2> version1_payload { SIDETONE_MIC_ID, mapped };
        std::span<const uint8_t> payload = sidetone_state->size() >= 4
            ? std::span<const uint8_t>(version2_payload)
            : std::span<const uint8_t>(version1_payload);

        if (auto sidetone_write = sendCenturionFeatureRequest(
                device_handle,
                static_cast<uint16_t>(protocols::CenturionFeature::HeadsetAudioSidetone),
                0x10,
                payload);
            !sidetone_write) {
            return sidetone_write.error();
        }

        return SidetoneResult {
            .current_level = level,
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = 0,
            .device_max    = SIDETONE_DEVICE_MAX,
            .is_muted      = level == 0
        };
    }

    Result<EqualizerResult> setEqualizer(hid_device* device_handle, const EqualizerSettings& settings) override
    {
        auto descriptor = readEqualizerDescriptor(device_handle);
        if (!descriptor) {
            return descriptor.error();
        }

        if (settings.size() != static_cast<int>(descriptor->bands.size())) {
            return DeviceError::invalidParameter("Equalizer requires one gain value per playback band");
        }

        std::vector<AdvancedEqBand> bands;
        bands.reserve(descriptor->bands.size());

        for (size_t i = 0; i < descriptor->bands.size(); ++i) {
            float gain = settings.bands[i];
            if (gain < descriptor->gain_min || gain > descriptor->gain_max) {
                return DeviceError::invalidParameter("Equalizer gain is outside the supported range");
            }

            bands.push_back(AdvancedEqBand {
                .frequency = descriptor->bands[i].frequency,
                .gain_db   = encodeGain(gain)
            });
        }

        if (auto write_result = writePlaybackAdvancedEq(device_handle, *descriptor, bands); !write_result) {
            return write_result.error();
        }

        return EqualizerResult {};
    }

    Result<EqualizerPresetResult> setEqualizerPreset(hid_device* device_handle, uint8_t preset) override
    {
        if (preset >= EQUALIZER_PRESETS_COUNT) {
            return DeviceError::invalidParameter("Device only supports presets 0-4");
        }

        static constexpr std::array<const std::array<float, 5>*, EQUALIZER_PRESETS_COUNT> PRESET_VALUES {
            &PRESET_FLAT,
            &PRESET_BASS_BOOST,
            &PRESET_TEAM_CHAT,
            &PRESET_SHOOTER,
            &PRESET_MOBA,
        };
        const auto* preset_values = PRESET_VALUES[preset];

        EqualizerSettings settings;
        settings.bands.assign(preset_values->begin(), preset_values->end());

        if (auto result = setEqualizer(device_handle, settings); !result) {
            return result.error();
        }

        return EqualizerPresetResult {
            .preset        = preset,
            .total_presets = EQUALIZER_PRESETS_COUNT
        };
    }

    Result<ParametricEqualizerResult> setParametricEqualizer(
        hid_device* device_handle,
        const ParametricEqualizerSettings& settings) override
    {
        auto descriptor = readEqualizerDescriptor(device_handle);
        if (!descriptor) {
            return descriptor.error();
        }

        if (settings.size() != static_cast<int>(descriptor->bands.size())) {
            return DeviceError::invalidParameter("Parametric equalizer requires exactly one entry per playback band");
        }

        std::vector<AdvancedEqBand> bands;
        bands.reserve(descriptor->bands.size());

        for (const auto& band : settings.bands) {
            if (band.type != EqualizerFilterType::Peaking) {
                return DeviceError::invalidParameter("This headset only supports peaking EQ bands");
            }

            if (std::fabs(band.q_factor - 1.0f) > 0.001f) {
                return DeviceError::invalidParameter("This headset uses a fixed Q factor of 1.0");
            }

            if (band.frequency < 20.0f || band.frequency > 20000.0f) {
                return DeviceError::invalidParameter("Frequency must be between 20 Hz and 20000 Hz");
            }

            if (band.gain < descriptor->gain_min || band.gain > descriptor->gain_max) {
                return DeviceError::invalidParameter("Gain is outside the supported range");
            }

            bands.push_back(AdvancedEqBand {
                .frequency = static_cast<uint16_t>(band.frequency),
                .gain_db   = encodeGain(band.gain),
                .q_factor  = static_cast<uint8_t>(std::clamp<long>(std::lround(band.q_factor), 1, 255))
            });
        }

        if (auto write_result = writePlaybackAdvancedEq(device_handle, *descriptor, bands); !write_result) {
            return write_result.error();
        }

        return ParametricEqualizerResult {};
    }

    Result<InactiveTimeResult> setInactiveTime(hid_device* device_handle, uint8_t minutes) override
    {
        if (auto write_result = sendCenturionFeatureRequest(
                device_handle,
                static_cast<uint16_t>(protocols::CenturionFeature::CenturionAutoSleep),
                0x10,
                std::array<uint8_t, 1> { minutes });
            !write_result) {
            return write_result.error();
        }

        return InactiveTimeResult {
            .minutes     = minutes,
            .min_minutes = 0,
            .max_minutes = 255
        };
    }

    static constexpr bool isAckPacket(std::span<const uint8_t> packet)
    {
        return packet.size() >= 2 && packet[0] == REPORT_PREFIX && packet[1] == 0x03;
    }

    static constexpr bool isPowerOffPacket(std::span<const uint8_t> packet)
    {
        return packet.size() >= 7 && packet[0] == REPORT_PREFIX && packet[1] == 0x05 && packet[6] == 0x00;
    }

    static constexpr bool isPowerEventPacket(std::span<const uint8_t> packet)
    {
        return packet.size() >= 2 && packet[0] == REPORT_PREFIX && packet[1] == 0x05;
    }

    static constexpr bool isBatteryResponsePacket(std::span<const uint8_t> packet)
    {
        return packet.size() >= 13 && packet[0] == REPORT_PREFIX && packet[1] == 0x0b && packet[8] == 0x04;
    }

    static Result<BatteryResult> parseBatteryResponse(std::span<const uint8_t> packet)
    {
        if (!isBatteryResponsePacket(packet)) {
            return DeviceError::protocolError("Unexpected battery response packet");
        }

        auto level = static_cast<int>(packet[10]);
        if (level > 100) {
            return DeviceError::protocolError("Battery percentage out of range");
        }

        auto status = packet[12] == 0x02 ? BATTERY_CHARGING : BATTERY_AVAILABLE;

        BatteryResult result {
            .level_percent = level,
            .status        = status,
        };

        return result;
    }

    static Result<BatteryResult> parseCenturionBatteryResponse(std::span<const uint8_t> packet)
    {
        if (packet.empty()) {
            return DeviceError::protocolError("Empty Centurion battery response");
        }

        auto level = static_cast<int>(packet[0]);
        if (level < 0 || level > 100) {
            return DeviceError::protocolError("Centurion battery percentage out of range");
        }

        auto charging_state = packet.size() >= 3 ? packet[2] : 0;
        // Centurion battery replies use states 1 and 2 for charging; the legacy packet parser only treats 0x02 as charging.
        auto status         = (charging_state == 1 || charging_state == 2)
            ? BATTERY_CHARGING
            : BATTERY_AVAILABLE;

        return BatteryResult {
            .level_percent = level,
            .status        = status,
        };
    }

private:
    enum class EqualizerBackend {
        AdvancedParametric,
        Onboard
    };

    // These presets match the values shown in Logitech G Hub for the 5-band
    // playback EQ at 80, 240, 750, 2200, and 6600 Hz.
    static constexpr std::array<float, 5> PRESET_FLAT { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    static constexpr std::array<float, 5> PRESET_BASS_BOOST { 4.0f, 2.0f, 0.0f, 0.0f, 0.0f };
    static constexpr std::array<float, 5> PRESET_TEAM_CHAT { -1.0f, 2.0f, 1.0f, 3.0f, 3.0f };
    static constexpr std::array<float, 5> PRESET_SHOOTER { -1.0f, -1.0f, 4.0f, 3.0f, 2.0f };
    static constexpr std::array<float, 5> PRESET_MOBA { 0.0f, 1.0f, 1.0f, 2.0f, 4.0f };

    static constexpr std::array<uint8_t, PACKET_SIZE> buildBatteryRequest()
    {
        std::array<uint8_t, PACKET_SIZE> request {};
        request[0] = REPORT_PREFIX;
        request[1] = 0x08;
        request[3] = 0x03;
        request[4] = 0x1a;
        request[6] = 0x03;
        request[8] = 0x04;
        request[9] = 0x0a;
        return request;
    }

    struct AdvancedEqBand {
        uint16_t frequency = 0;
        int8_t gain_db     = 0;
        uint8_t q_factor   = 1;
    };

    struct AdvancedEqDescriptor {
        EqualizerBackend backend = EqualizerBackend::AdvancedParametric;
        uint8_t active_slot = 0;
        float gain_min      = -12.0f;
        float gain_max      = 12.0f;
        std::vector<AdvancedEqBand> bands;
    };

public:
    static std::vector<uint8_t> buildOnboardEqPayloadForTest(
        uint8_t slot,
        const std::vector<std::tuple<uint16_t, int8_t, uint8_t>>& bands)
    {
        std::vector<AdvancedEqBand> converted;
        converted.reserve(bands.size());
        for (const auto& [frequency, gain_db, q_factor] : bands) {
            converted.push_back(AdvancedEqBand {
                .frequency = frequency,
                .gain_db   = gain_db,
                .q_factor  = q_factor
            });
        }
        return buildOnboardEqPayload(slot, converted);
    }

    static std::vector<uint16_t> quantizeOnboardEqCoefficientsForTest(const std::array<double, 5>& coeffs)
    {
        return quantizeOnboardEqCoefficients(coeffs);
    }

private:

    void cacheEqualizerInfo(const AdvancedEqDescriptor& descriptor) const
    {
        cached_band_count_      = static_cast<int>(descriptor.bands.size());
        cached_gain_min_        = static_cast<int>(descriptor.gain_min);
        cached_gain_max_        = static_cast<int>(descriptor.gain_max);
        has_cached_equalizer_info_ = true;
    }

    static constexpr std::array<uint8_t, 2> buildPlaybackEqSelector(uint8_t slot)
    {
        return { PLAYBACK_DIRECTION, slot };
    }

    static constexpr int8_t decodeSignedByte(uint8_t value)
    {
        return static_cast<int8_t>(value);
    }

    static constexpr int8_t encodeGain(float gain)
    {
        return static_cast<int8_t>(gain);
    }

    static std::array<double, 5> buildPeakingEqBiquad(double frequency, double gain_db, double q_factor, double sample_rate)
    {
        constexpr double pi = 3.14159265358979323846;
        double amplitude = std::pow(10.0, gain_db / 40.0);
        double w0        = 2.0 * pi * frequency / sample_rate;
        double cos_w0    = std::cos(w0);
        double alpha     = std::sin(w0) / (2.0 * q_factor);
        double a0        = 1.0 + alpha / amplitude;

        return {
            (1.0 + alpha * amplitude) / a0,
            (-2.0 * cos_w0) / a0,
            (1.0 - alpha * amplitude) / a0,
            (-2.0 * cos_w0) / a0,
            (1.0 - alpha / amplitude) / a0
        };
    }

    static std::vector<uint16_t> quantizeOnboardEqCoefficients(const std::array<double, 5>& coeffs)
    {
        static constexpr std::array<double, 5> scales {
            2147483648.0,
            1073741824.0,
            2147483648.0,
            1073741824.0,
            2147483648.0
        };

        std::vector<uint16_t> words;
        words.reserve(10);

        for (size_t i = 0; i < coeffs.size(); ++i) {
            auto q_value = static_cast<int64_t>(std::llround(coeffs[i] * scales[i]));
            q_value      = std::clamp<int64_t>(q_value, -(1LL << 31), (1LL << 31) - 1);
            q_value &= ~INT64_C(0xFF);
            words.push_back(static_cast<uint16_t>((q_value >> 16) & 0xFFFF));
            words.push_back(static_cast<uint16_t>(q_value & 0xFFFF));
        }

        return words;
    }

    static std::vector<uint8_t> buildOnboardEqCoefficientSection(
        const std::vector<AdvancedEqBand>& bands,
        double sample_rate,
        uint8_t section_type)
    {
        static constexpr double HEADROOM = 1.19;

        std::vector<std::array<double, 5>> raw_coeffs;
        raw_coeffs.reserve(bands.size());

        double max_b0 = 1.0;
        for (const auto& band : bands) {
            double q_factor = std::max(0.1, static_cast<double>(band.q_factor));
            auto coeffs     = buildPeakingEqBiquad(band.frequency, band.gain_db, q_factor, sample_rate);
            max_b0          = std::max(max_b0, std::abs(coeffs[0]));
            raw_coeffs.push_back(coeffs);
        }

        double rescale = std::max(1.0, max_b0) * HEADROOM;

        std::vector<uint16_t> words;
        words.reserve(1 + bands.size() * 10 + 2);
        words.push_back(static_cast<uint16_t>(bands.size()));

        for (const auto& coeffs : raw_coeffs) {
            std::array<double, 5> normalized {
                coeffs[0] / rescale,
                coeffs[1] / rescale,
                coeffs[2] / rescale,
                coeffs[3],
                coeffs[4]
            };

            auto quantized = quantizeOnboardEqCoefficients(normalized);
            words.insert(words.end(), quantized.begin(), quantized.end());
        }

        auto rescale_q = static_cast<int64_t>(std::llround(rescale * 67108864.0));
        rescale_q      = std::clamp<int64_t>(rescale_q, -(1LL << 31), (1LL << 31) - 1);
        rescale_q &= 0xFFFFFF00;
        words.push_back(static_cast<uint16_t>((rescale_q >> 16) & 0xFFFF));
        words.push_back(static_cast<uint16_t>(rescale_q & 0xFFFF));

        uint16_t coeff_count = static_cast<uint16_t>(bands.size() * 10 + 3);
        std::vector<uint8_t> section {
            section_type,
            0x00,
            static_cast<uint8_t>(coeff_count & 0xFF),
            static_cast<uint8_t>((coeff_count >> 8) & 0xFF)
        };

        section.reserve(section.size() + words.size() * 2);
        for (uint16_t word : words) {
            section.push_back(static_cast<uint8_t>(word & 0xFF));
            section.push_back(static_cast<uint8_t>((word >> 8) & 0xFF));
        }

        return section;
    }

    static std::vector<uint8_t> buildOnboardEqPayload(uint8_t slot, const std::vector<AdvancedEqBand>& bands)
    {
        std::vector<uint8_t> payload {
            slot,
            static_cast<uint8_t>(bands.size())
        };

        for (const auto& band : bands) {
            payload.push_back(static_cast<uint8_t>((band.frequency >> 8) & 0xFF));
            payload.push_back(static_cast<uint8_t>(band.frequency & 0xFF));
            payload.push_back(static_cast<uint8_t>(band.gain_db));
            payload.push_back(band.q_factor);
        }

        payload.insert(payload.end(), { 0x05, 0x5A, 0xE3, 0x00 });
        payload.insert(payload.end(), { 0x03, 0x0E, 0x00, 0x02, 0x00, 0x00, 0x00 });

        auto playback_section = buildOnboardEqCoefficientSection(bands, 48000.0, 0x01);
        payload.insert(payload.end(), playback_section.begin(), playback_section.end());

        auto microphone_section = buildOnboardEqCoefficientSection(bands, 16000.0, 0x02);
        payload.insert(payload.end(), microphone_section.begin(), microphone_section.end());

        return payload;
    }

    Result<AdvancedEqDescriptor> readAdvancedEqDescriptor(hid_device* device_handle) const
    {
        auto info_reply = sendCenturionFeatureRequest(
            device_handle,
            static_cast<uint16_t>(protocols::CenturionFeature::HeadsetAdvancedParaEQ),
            0x00);
        if (!info_reply) {
            return info_reply.error();
        }

        if (info_reply->size() < 5) {
            return DeviceError::protocolError("Advanced EQ info response was too short");
        }

        auto active_slot_reply = sendCenturionFeatureRequest(
            device_handle,
            static_cast<uint16_t>(protocols::CenturionFeature::HeadsetAdvancedParaEQ),
            0x30,
            std::array<uint8_t, 1> { PLAYBACK_DIRECTION });
        if (!active_slot_reply) {
            return active_slot_reply.error();
        }
        if (active_slot_reply->empty()) {
            return DeviceError::protocolError("Advanced EQ active slot response was empty");
        }

        auto params_reply = sendCenturionFeatureRequest(
            device_handle,
            static_cast<uint16_t>(protocols::CenturionFeature::HeadsetAdvancedParaEQ),
            0x10,
            buildPlaybackEqSelector((*active_slot_reply)[0]));
        if (!params_reply) {
            return params_reply.error();
        }

        AdvancedEqDescriptor descriptor {
            .backend     = EqualizerBackend::AdvancedParametric,
            .active_slot = (*active_slot_reply)[0],
            .gain_min    = static_cast<float>(decodeSignedByte((*info_reply)[3])),
            .gain_max    = static_cast<float>(decodeSignedByte((*info_reply)[4])),
        };

        for (size_t offset = 0; offset + 2 < params_reply->size(); offset += 3) {
            auto frequency = static_cast<uint16_t>(
                (static_cast<uint16_t>((*params_reply)[offset]) << 8)
                | static_cast<uint16_t>((*params_reply)[offset + 1]));
            if (frequency == 0) {
                break;
            }

            descriptor.bands.push_back(AdvancedEqBand {
                .frequency = frequency,
                .gain_db   = decodeSignedByte((*params_reply)[offset + 2])
            });
        }

        if (descriptor.bands.empty()) {
            return DeviceError::protocolError("Advanced EQ band response was empty");
        }

        cacheEqualizerInfo(descriptor);
        return descriptor;
    }

    Result<AdvancedEqDescriptor> readOnboardEqDescriptor(hid_device* device_handle) const
    {
        auto info_reply = sendCenturionFeatureRequest(
            device_handle,
            static_cast<uint16_t>(protocols::CenturionFeature::HeadsetOnboardEQ),
            0x00);
        if (!info_reply) {
            return info_reply.error();
        }

        if (info_reply->size() < 5) {
            return DeviceError::protocolError("Onboard EQ info response was too short");
        }

        auto params_reply = sendCenturionFeatureRequest(
            device_handle,
            static_cast<uint16_t>(protocols::CenturionFeature::HeadsetOnboardEQ),
            0x10,
            std::array<uint8_t, 1> { 0x00 });
        if (!params_reply) {
            return params_reply.error();
        }

        if (params_reply->size() < 2) {
            return DeviceError::protocolError("Onboard EQ parameter response was too short");
        }

        AdvancedEqDescriptor descriptor {
            .backend     = EqualizerBackend::Onboard,
            .active_slot = 0x00,
            .gain_min    = -12.0f,
            .gain_max    = 12.0f,
        };

        uint8_t band_count = (*params_reply)[1];
        size_t offset      = 2;
        for (uint8_t band = 0; band < band_count && offset + 3 < params_reply->size(); ++band) {
            descriptor.bands.push_back(AdvancedEqBand {
                .frequency = static_cast<uint16_t>((static_cast<uint16_t>((*params_reply)[offset]) << 8) | (*params_reply)[offset + 1]),
                .gain_db   = decodeSignedByte((*params_reply)[offset + 2]),
                .q_factor  = (*params_reply)[offset + 3]
            });
            offset += 4;
        }

        if (descriptor.bands.empty()) {
            return DeviceError::protocolError("Onboard EQ band response was empty");
        }

        cacheEqualizerInfo(descriptor);
        return descriptor;
    }

    Result<AdvancedEqDescriptor> readEqualizerDescriptor(hid_device* device_handle) const
    {
        if (auto advanced = readAdvancedEqDescriptor(device_handle); advanced) {
            return advanced;
        }

        return readOnboardEqDescriptor(device_handle);
    }

    Result<void> writePlaybackAdvancedEq(
        hid_device* device_handle,
        const AdvancedEqDescriptor& descriptor,
        const std::vector<AdvancedEqBand>& bands) const
    {
        if (descriptor.backend == EqualizerBackend::AdvancedParametric) {
            std::vector<uint8_t> payload;
            payload.reserve(2 + bands.size() * 3);
            payload.push_back(PLAYBACK_DIRECTION);
            payload.push_back(descriptor.active_slot);

            for (const auto& band : bands) {
                payload.push_back(static_cast<uint8_t>((band.frequency >> 8) & 0xFF));
                payload.push_back(static_cast<uint8_t>(band.frequency & 0xFF));
                payload.push_back(static_cast<uint8_t>(band.gain_db));
            }

            if (auto write_reply = sendCenturionFeatureRequest(
                    device_handle,
                    static_cast<uint16_t>(protocols::CenturionFeature::HeadsetAdvancedParaEQ),
                    0x20,
                    payload);
                !write_reply) {
                return write_reply.error();
            }

            return {};
        }

        auto payload = buildOnboardEqPayload(descriptor.active_slot, bands);
        if (auto write_reply = sendCenturionFeatureRequest(
                device_handle,
                static_cast<uint16_t>(protocols::CenturionFeature::HeadsetOnboardEQ),
                0x20,
                payload);
            !write_reply) {
            return write_reply.error();
        }

        return {};
    }

    mutable bool has_cached_equalizer_info_ = false;
    mutable int cached_band_count_          = 0;
    mutable int cached_gain_min_            = 0;
    mutable int cached_gain_max_            = 0;
};

} // namespace headsetcontrol
