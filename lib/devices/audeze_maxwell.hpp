#pragma once

#include "audeze_maxwell_device.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <format>
#include <numbers>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Audeze Maxwell Gaming Headset
 *
 * Features:
 * - Sidetone (level read from the headset, 32 levels by default)
 * - Inactive time with discrete levels
 * - Volume limiter
 * - Equalizer presets (10 total: 6 default + 4 custom)
 * - Custom EQ (ten whole-dB gains, edits the selected custom preset)
 * - Battery status
 * - Chatmix
 * - Voice prompts
 * - Mic noise filter
 *
 * The protocol is implemented in audeze_maxwell_device.hpp. The custom EQ adds these
 * commands:
 *
 *   read stored data   0C 0A <key> E8 03       -> 5B: 0C 0A <status> <key> <length> <data...>
 *   reclaim storage    03 0A <length>          -> 5B: 03 0A <unknown>
 *   store data         0D 0A <key> <data...>   -> 5B: 0D 0A <status> <key>
 *   apply to the DSP   03 0E 00 <coefficients> -> 5B: 03 0E <status>
 *
 * <key> and <length> are 16-bit, low byte first, and <status> is 00 on success. The
 * byte in the reclaim reply is not a status; the dongle sends 01 in normal operation.
 *
 * Each custom preset has two stored items: the settings the Audeze app shows
 * (EF00 + slot) and the DSP coefficients computed from them (E42C + slot).
 */
class AudezeMaxwell : public AudezeMaxwellDevice {
public:
    static constexpr std::array<uint16_t, 2> SUPPORTED_PRODUCT_IDS {
        0x4b19, // Maxwell
        0x4b18, // Maxwell Xbox Dongle
    };

    static constexpr uint8_t VOLUME_LIMITER_ON  = 0x88;
    static constexpr uint8_t VOLUME_LIMITER_OFF = 0x8E;

    // Presets as the headset numbers them (1-10); the last four are the custom ones
    static constexpr uint8_t FIRST_CUSTOM_PRESET = 7;
    static constexpr uint8_t LAST_CUSTOM_PRESET  = 10;

    static constexpr Command CMD_EQ_APPLY { 0x03, 0x0E };
    static constexpr Command CMD_STORAGE_RECLAIM { 0x03, 0x0A };
    static constexpr Command CMD_STORAGE_READ { 0x0C, 0x0A };
    static constexpr Command CMD_STORAGE_WRITE { 0x0D, 0x0A };

    static constexpr uint16_t STORAGE_KEY_EQ_SETTINGS     = 0xEF00;
    static constexpr uint16_t STORAGE_KEY_EQ_COEFFICIENTS = 0xE42C;
    static constexpr uint16_t STORAGE_READ_MAX_LENGTH     = 1000;

    std::vector<uint16_t> getProductIds() const override
    {
        return { SUPPORTED_PRODUCT_IDS.begin(), SUPPORTED_PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override
    {
        return "Audeze Maxwell"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_SIDETONE) | B(CAP_INACTIVE_TIME) | B(CAP_VOLUME_LIMITER)
            | B(CAP_EQUALIZER_PRESET) | B(CAP_BATTERY_STATUS) | B(CAP_CHATMIX_STATUS)
            | B(CAP_VOICE_PROMPTS) | B(CAP_NOISE_FILTER) | B(CAP_SIDETONE_STATUS) | B(CAP_EQUALIZER);
    }

    constexpr capability_detail getCapabilityDetail([[maybe_unused]] enum capabilities cap) const override
    {
        return { .usagepage = 0xff13, .usageid = 0x1, .interface_id = 0 };
    }

    Result<VolumeLimiterResult> setVolumeLimiter(hid_device* device_handle, bool enabled) override
    {
        if (auto result = writeKey(device_handle, KEY_VOLUME_LIMITER, enabled ? VOLUME_LIMITER_ON : VOLUME_LIMITER_OFF); !result) {
            return result.error();
        }

        return VolumeLimiterResult { .enabled = enabled };
    }

    std::optional<EqualizerInfo> getEqualizerInfo() const override
    {
        return EqualizerInfo {
            .bands_count    = static_cast<int>(EQ_FREQUENCIES.size()),
            .bands_baseline = 0,
            .bands_step     = 1.0f,
            .bands_min      = static_cast<int>(EQ_GAIN_MIN),
            .bands_max      = static_cast<int>(EQ_GAIN_MAX)
        };
    }

    /**
     * @brief Change the gains of the custom preset that is currently selected
     *
     * Follows Audeze.exe writeEqBands: apply the new coefficients, store the settings,
     * store the coefficients. Each command is sent exactly once. A later failure can
     * leave the active EQ or one stored item changed, so it is reported and never
     * presented as an atomic update. Both stored items are read back at the end.
     */
    Result<EqualizerResult> setEqualizer(hid_device* device_handle, const EqualizerSettings& settings) override
    {
        if (settings.bands.size() != EQ_FREQUENCIES.size() || !std::ranges::all_of(settings.bands, isSupportedEqGain)) {
            return DeviceError::invalidParameter("Maxwell EQ needs ten whole-dB gains from -12 to 12");
        }

        auto slot = readSelectedCustomSlot(device_handle);
        if (!slot) {
            return slot.error();
        }
        const auto settings_key     = static_cast<uint16_t>(STORAGE_KEY_EQ_SETTINGS + *slot);
        const auto coefficients_key = static_cast<uint16_t>(STORAGE_KEY_EQ_COEFFICIENTS + *slot);

        // Only the gains are replaced; everything else the app stored is kept
        auto stored_settings = readStoredData(device_handle, settings_key, EQ_SETTINGS_SIZE);
        if (!stored_settings) {
            return stored_settings.error();
        }
        auto bands = readEqBands(*stored_settings);
        if (!bands) {
            return bands.error();
        }
        for (size_t i = 0; i < bands->size(); ++i) {
            (*bands)[i].gain = settings.bands[i];
            writeEqBandGain(*stored_settings, i, settings.bands[i]);
        }
        auto coefficients = eqCoefficients(*bands);
        if (!coefficients) {
            return coefficients.error();
        }

        if (auto result = applyEq(device_handle, *coefficients); !result) {
            return result.error();
        }
        if (auto result = storeData(device_handle, settings_key, *stored_settings); !result) {
            return result.error();
        }
        if (auto result = storeData(device_handle, coefficients_key, *coefficients); !result) {
            return result.error();
        }

        if (auto result = verifyStoredData(device_handle, settings_key, *stored_settings, "Maxwell EQ readback differs from the saved settings; update may be partial"); !result) {
            return result.error();
        }
        if (auto result = verifyStoredData(device_handle, coefficients_key, *coefficients, "Maxwell coefficient readback differs; EQ update may be partial"); !result) {
            return result.error();
        }
        return EqualizerResult {};
    }

protected:
    // Non-static so tests can shorten them
    std::chrono::milliseconds eq_timeout_ { 1500 };
    std::chrono::milliseconds eq_fragment_interval_ { 20 };

private:
    // Positions in the replies of the storage commands: 05 5B <length> <command> <status> <key> <length> <data...>
    static constexpr size_t STORAGE_STATUS_OFFSET = 6;
    static constexpr size_t STORAGE_KEY_OFFSET    = 7;
    static constexpr size_t STORAGE_LENGTH_OFFSET = 9;
    static constexpr size_t STORAGE_DATA_OFFSET   = 11;

    /**
     * EQ requests are never repeated: storing twice is not known to be safe. The reports
     * are paced because the wireless dongle can silently drop an unpaced burst of long
     * command fragments (confirmed by live EQ apply/restore testing).
     */
    RequestOptions eqRequestOptions() const
    {
        return { .attempts = 1, .timeout = eq_timeout_, .fragment_interval = eq_fragment_interval_ };
    }

    /// Slot (0-3) of the selected custom preset; an error if a built-in preset is selected
    Result<uint8_t> readSelectedCustomSlot(hid_device* device_handle) const
    {
        auto preset = readKey(device_handle, KEY_EQ_PRESET);
        if (!preset) {
            return preset.error();
        }
        if (*preset < FIRST_CUSTOM_PRESET || *preset > LAST_CUSTOM_PRESET) {
            return DeviceError::invalidParameter("Select a Maxwell custom preset with -p 6..9 before editing EQ");
        }
        return static_cast<uint8_t>(*preset - FIRST_CUSTOM_PRESET);
    }

    Result<Message> readStoredData(hid_device* device_handle, uint16_t key, size_t expected_size) const
    {
        auto body = commandBody(CMD_STORAGE_READ);
        appendLE16(body, key);
        appendLE16(body, STORAGE_READ_MAX_LENGTH);

        // A reply too short to carry a key is accepted so it is reported as invalid below
        auto reply = request(
            device_handle, body, [key](const Message& message) {
                return isMessage(message, TYPE_RESPONSE, CMD_STORAGE_READ)
                    && (message.size() < STORAGE_LENGTH_OFFSET || hasId(message, STORAGE_KEY_OFFSET, key));
            },
            eqRequestOptions());
        if (!reply) {
            return reply.error();
        }
        if (reply->size() != STORAGE_DATA_OFFSET + expected_size
            || (*reply)[STORAGE_STATUS_OFFSET] != STATUS_OK
            || readLE16(*reply, STORAGE_LENGTH_OFFSET) != expected_size) {
            return DeviceError::protocolError("Maxwell returned an invalid custom EQ slot");
        }
        return Message(reply->begin() + STORAGE_DATA_OFFSET, reply->end());
    }

    /// Read stored data back and compare it with what was written
    Result<void> verifyStoredData(hid_device* device_handle, uint16_t key, const Message& expected, std::string_view mismatch) const
    {
        auto stored = readStoredData(device_handle, key, expected.size());
        if (!stored) {
            return stored.error();
        }
        if (*stored != expected) {
            return DeviceError::protocolError(mismatch);
        }
        return {};
    }

    /**
     * @brief Send an EQ command and check its acknowledgement
     *
     * @param key     Storage key the acknowledgement has to repeat, if the command has one
     * @param checked Whether byte 6 of the acknowledgement is a status that must be zero
     */
    Result<void> eqCommand(hid_device* device_handle, const Message& body, std::string_view operation,
        std::optional<uint16_t> key = std::nullopt, bool checked = true) const
    {
        const Command command { body[0], body[1] };
        const size_t reply_size = key ? STORAGE_KEY_OFFSET + 2 : STORAGE_STATUS_OFFSET + 1;

        auto reply = request(
            device_handle, body, [command, key](const Message& message) {
                return isMessage(message, TYPE_RESPONSE, command)
                    && (!key || message.size() < STORAGE_LENGTH_OFFSET || hasId(message, STORAGE_KEY_OFFSET, *key));
            },
            eqRequestOptions());
        if (!reply) {
            auto error    = reply.error();
            error.message = std::format("Maxwell {} failed: {}; EQ update may be partial. Close Audeze before retrying.", operation, error.message);
            return error;
        }
        if (reply->size() != reply_size || (checked && (*reply)[STORAGE_STATUS_OFFSET] != STATUS_OK)) {
            return DeviceError::protocolError(std::format("Maxwell rejected {}; EQ update may be partial", operation));
        }
        return {};
    }

    /// Make the DSP use new coefficients right away, without storing them
    Result<void> applyEq(hid_device* device_handle, std::span<const uint8_t> coefficients) const
    {
        auto body = commandBody(CMD_EQ_APPLY, { 0x00 });
        body.insert(body.end(), coefficients.begin(), coefficients.end());
        return eqCommand(device_handle, body, "apply EQ");
    }

    Result<void> reclaimStorage(hid_device* device_handle, size_t length) const
    {
        auto body = commandBody(CMD_STORAGE_RECLAIM);
        appendLE16(body, static_cast<uint16_t>(length));

        // Unlike apply/save, Audeze's reclaim helper does not interpret byte 6
        // as a zero-success status (RVA 24740 / predicate 1B570). The dongle
        // replies 01 here during normal operation. Framing and opcode are
        // validated; the saves and the byte-for-byte readback catch the rest.
        return eqCommand(device_handle, body, "prepare EQ storage", std::nullopt, false);
    }

    /// Store data under a key, reclaiming the space for it first as Audeze.exe does
    Result<void> storeData(hid_device* device_handle, uint16_t key, std::span<const uint8_t> data) const
    {
        if (auto result = reclaimStorage(device_handle, data.size()); !result) {
            return result.error();
        }

        auto body = commandBody(CMD_STORAGE_WRITE);
        appendLE16(body, key);
        body.insert(body.end(), data.begin(), data.end());
        return eqCommand(device_handle, body, "save EQ", key);
    }

    // ------------------------------------------------------------------------
    // Custom EQ data: the settings the Audeze app stores for a custom preset and
    // the DSP coefficients computed from them. Static and without HID access, so
    // it can be checked on its own.
    // ------------------------------------------------------------------------
public:
    static constexpr std::array<int, 10> EQ_FREQUENCIES { 32, 64, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };

    static constexpr double EQ_GAIN_MIN      = -12;
    static constexpr double EQ_GAIN_MAX      = 12;
    static constexpr double EQ_MAX_BANDWIDTH = 48000;

    // The stored settings are a header followed by one record per band. Only the
    // fields below are interpreted; everything else is kept as it is.
    static constexpr size_t EQ_HEADER_SIZE           = 5;
    static constexpr size_t EQ_BAND_RECORD_SIZE      = 18;
    static constexpr size_t EQ_BAND_FREQUENCY_OFFSET = 2;
    static constexpr size_t EQ_BAND_GAIN_OFFSET      = 6;
    static constexpr size_t EQ_BAND_BANDWIDTH_OFFSET = 10;
    static constexpr size_t EQ_SETTINGS_SIZE         = EQ_HEADER_SIZE + EQ_BAND_RECORD_SIZE * EQ_FREQUENCIES.size();

    struct EqBand {
        double frequency;
        double gain;
        double bandwidth;
    };

    /// True for the gains the app can produce: whole dB within the supported range
    static bool isSupportedEqGain(double gain)
    {
        return std::isfinite(gain) && gain >= EQ_GAIN_MIN && gain <= EQ_GAIN_MAX && std::round(gain) == gain;
    }

    /// Parse the bands out of the stored settings
    static Result<std::vector<EqBand>> readEqBands(std::span<const uint8_t> settings)
    {
        if (settings.size() != EQ_SETTINGS_SIZE) {
            return DeviceError::protocolError("Unexpected Maxwell custom EQ size (expected ten bands)");
        }
        std::vector<EqBand> bands;
        for (size_t i = 0; i < EQ_FREQUENCIES.size(); ++i) {
            const size_t record = eqBandOffset(i);
            EqBand band {
                .frequency = readEqBandValue(settings, record + EQ_BAND_FREQUENCY_OFFSET),
                .gain      = readEqBandValue(settings, record + EQ_BAND_GAIN_OFFSET),
                .bandwidth = readEqBandValue(settings, record + EQ_BAND_BANDWIDTH_OFFSET)
            };
            if (band.frequency != EQ_FREQUENCIES[i] || band.bandwidth <= 0 || band.bandwidth > EQ_MAX_BANDWIDTH) {
                return DeviceError::protocolError("Unsupported Maxwell EQ band layout or bandwidth");
            }
            bands.push_back(band);
        }
        return bands;
    }

    /// Store a whole-dB gain in a band's record, leaving the rest of the settings untouched
    static void writeEqBandGain(std::span<uint8_t> settings, size_t band, double gain)
    {
        const auto hundredths = static_cast<int32_t>(gain * EQ_VALUES_PER_UNIT);
        writeLE32(settings, eqBandOffset(band) + EQ_BAND_GAIN_OFFSET, static_cast<uint32_t>(hundredths));
    }

    /// Coefficient block for one sample rate: band count, five words per band, output gain
    static Result<Message> eqCoefficientsForRate(std::span<const EqBand> bands, int rate)
    {
        const bool known_rate = std::ranges::any_of(EQ_SAMPLE_RATES, [rate](const EqSampleRate& supported) {
            return supported.rate == rate;
        });
        if (bands.empty() || bands.size() > EQ_FREQUENCIES.size() || !known_rate) {
            return DeviceError::invalidParameter("Invalid Maxwell EQ band count or sample rate");
        }
        if (!std::ranges::all_of(bands, [rate](const EqBand& band) { return isSupportedEqBand(band, rate); })) {
            return DeviceError::invalidParameter("Maxwell EQ requires finite whole-dB gains from -12 to 12");
        }

        // The app uses integral dB gains. Its DLL orders descending gain, retaining
        // input order for ties. Fractional gains are deliberately not exposed here.
        std::vector<EqBand> ordered(bands.begin(), bands.end());
        std::stable_sort(ordered.begin(), ordered.end(), [](const EqBand& a, const EqBand& b) { return a.gain > b.gain; });
        std::vector<EqFilter> filters;
        for (const auto& band : ordered) {
            filters.push_back(peakingFilter(band, rate));
        }

        const auto peaks      = cascadePeaks(filters, rate);
        const double peak     = peaks.back();
        const double prescale = std::max(1.0, peak / EQ_STAGE_PEAK_LIMIT);
        double scale_product  = 1;

        Message result;
        appendLE16(result, static_cast<uint16_t>(bands.size()));
        for (size_t i = 0; i < filters.size(); ++i) {
            auto filter              = filters[i];
            double scale             = std::max({ std::abs(filter[COEF_B0]), std::abs(filter[COEF_B1] * 0.5), std::abs(filter[COEF_B2]) });
            const double first_scale = i == 0 ? prescale * EQ_FIRST_STAGE_MARGIN : 1.0;
            if (peaks[i] / (scale_product * scale * first_scale) > EQ_STAGE_PEAK_LIMIT) {
                scale = std::max(1.0, scale);
            }
            scale *= first_scale;
            scale_product *= scale;

            // Only the feed-forward coefficients are scaled; all of them are rounded to 24 bits
            for (size_t j = 0; j < filter.size(); ++j) {
                if (j < COEF_A1) {
                    filter[j] /= scale;
                }
                const double quantum = hasHalfResolution(j) ? Q22 : Q23;
                filter[j]            = std::floor(std::clamp(filter[j] * quantum + 0.5, -Q23, Q23 - 1)) / quantum;
            }
            if (!isStableFilter(filter)) {
                return DeviceError::invalidParameter("Maxwell EQ filter becomes unstable after quantization");
            }
            for (size_t j = 0; j < filter.size(); ++j) {
                appendDsp32(result, static_cast<int32_t>(filter[j] * (hasHalfResolution(j) ? Q30 : Q31)));
            }
        }

        // Mode 1, requested rescaling 0 dB. Limit the overall peak, matching
        // change_rescale_cofe. Gain is rounded at Q18 and stored at Q26.
        const double gain = scale_product * std::min(1.0, EQ_OUTPUT_PEAK_LIMIT / peak);
        if (!std::isfinite(gain) || gain <= 0 || gain >= 32) {
            return DeviceError::invalidParameter("Maxwell EQ gain exceeds the DSP range");
        }
        const auto fixed_gain = static_cast<int32_t>(std::floor(gain * Q18 + 0.5));
        if (fixed_gain > Q23 - 1) {
            return DeviceError::invalidParameter("Maxwell EQ rounded gain exceeds the DSP range");
        }
        appendDsp32(result, fixed_gain * 256);
        return result;
    }

    /// Coefficient payload for all sample rates, as applied to the DSP and stored on the headset
    static Result<Message> eqCoefficients(std::span<const EqBand> bands)
    {
        Message result { static_cast<uint8_t>(EQ_SAMPLE_RATES.size()), 0, 0, 0 };
        for (const auto& sample_rate : EQ_SAMPLE_RATES) {
            auto block = eqCoefficientsForRate(bands, sample_rate.rate);
            if (!block) {
                return block.error();
            }
            appendLE16(result, sample_rate.id);
            appendLE16(result, static_cast<uint16_t>(block->size() / 2));
            result.insert(result.end(), block->begin(), block->end());
        }
        return result;
    }

private:
    // Band fields are signed 32-bit little endian values in hundredths (Hz or dB)
    static constexpr double EQ_VALUE_SCALE  = 0.01;
    static constexpr int EQ_VALUES_PER_UNIT = 100;

    // Sample rates the headset needs coefficients for, with the ID each one has on the wire
    struct EqSampleRate {
        int rate;
        uint8_t id;
    };
    static constexpr std::array<EqSampleRate, 4> EQ_SAMPLE_RATES { { { 44100, 1 }, { 48000, 2 }, { 88200, 5 }, { 96000, 6 } } };

    // Fixed point scales of the DSP
    static constexpr double Q18 = 262144.0;
    static constexpr double Q22 = 4194304.0;
    static constexpr double Q23 = 8388608.0;
    static constexpr double Q30 = 1073741824.0;
    static constexpr double Q31 = 2147483648.0;

    // Headroom rules of AirohaPeqLibrary.dll
    static constexpr double EQ_STAGE_PEAK_LIMIT   = 4.0;
    static constexpr double EQ_OUTPUT_PEAK_LIMIT  = 8.0; // 18.06 dB
    static constexpr double EQ_FIRST_STAGE_MARGIN = 1.19;

    // Biquad coefficients, normalised to a0 = 1
    enum EqCoefficient : size_t {
        COEF_B0,
        COEF_B1,
        COEF_B2,
        COEF_A1,
        COEF_A2
    };
    using EqFilter = std::array<double, 5>;

    static uint32_t readLE32(std::span<const uint8_t> bytes, size_t offset)
    {
        return uint32_t(bytes[offset]) | (uint32_t(bytes[offset + 1]) << 8)
            | (uint32_t(bytes[offset + 2]) << 16) | (uint32_t(bytes[offset + 3]) << 24);
    }

    static void writeLE32(std::span<uint8_t> bytes, size_t offset, uint32_t value)
    {
        for (size_t byte = 0; byte < 4; ++byte) {
            bytes[offset + byte] = static_cast<uint8_t>(value >> (8 * byte));
        }
    }

    // DSP words are high halfword first, with each halfword little-endian.
    static void appendDsp32(Message& bytes, int32_t value)
    {
        const auto bits = static_cast<uint32_t>(value);
        appendLE16(bytes, static_cast<uint16_t>(bits >> 16));
        appendLE16(bytes, static_cast<uint16_t>(bits));
    }

    /// Start of a band's record in the stored settings
    static constexpr size_t eqBandOffset(size_t band)
    {
        return EQ_HEADER_SIZE + EQ_BAND_RECORD_SIZE * band;
    }

    static double readEqBandValue(std::span<const uint8_t> settings, size_t offset)
    {
        return std::bit_cast<int32_t>(readLE32(settings, offset)) * EQ_VALUE_SCALE;
    }

    static bool isSupportedEqBand(const EqBand& band, int rate)
    {
        return std::isfinite(band.frequency) && std::isfinite(band.bandwidth)
            && band.frequency > 0 && band.frequency < rate / 2
            && band.bandwidth > 0 && band.bandwidth <= EQ_MAX_BANDWIDTH
            && isSupportedEqGain(band.gain);
    }

    static EqFilter peakingFilter(const EqBand& band, double rate)
    {
        const double omega     = band.frequency * (2 * std::numbers::pi) / rate;
        const double amplitude = std::sqrt(std::pow(10.0, band.gain / 20.0));
        const double alpha     = std::sin(omega) / (2 * (band.frequency / band.bandwidth));
        const double a0        = 1 + alpha / amplitude;
        const double b1        = -2 * std::cos(omega) / a0;
        return { (1 + alpha * amplitude) / a0, b1, (1 - alpha * amplitude) / a0,
            b1, (1 - alpha / amplitude) / a0 };
    }

    static std::complex<double> filterResponse(const EqFilter& filter, std::complex<double> z)
    {
        return (filter[COEF_B0] + filter[COEF_B1] * z + filter[COEF_B2] * z * z)
            / (1.0 + filter[COEF_A1] * z + filter[COEF_A2] * z * z);
    }

    static bool isStableFilter(const EqFilter& filter)
    {
        // Jury stability conditions for z^2 + a1*z + a2.
        return std::abs(filter[COEF_A2]) < 1 && 1 + filter[COEF_A1] + filter[COEF_A2] > 0
            && 1 - filter[COEF_A1] + filter[COEF_A2] > 0;
    }

    /// b1 and a1 are stored with one fractional bit less than the other coefficients
    static constexpr bool hasHalfResolution(size_t coefficient)
    {
        return coefficient == COEF_B1 || coefficient == COEF_A1;
    }

    /**
     * @brief Peak gain of each prefix of a filter cascade
     *
     * Element i is the peak of filters 0..i in series, on the DLL's 1 Hz grid (DC inclusive,
     * Nyquist exclusive). It reproduces the per-stage headroom decisions without global state.
     */
    static std::vector<double> cascadePeaks(std::span<const EqFilter> filters, int rate)
    {
        std::vector<double> peaks(filters.size(), 0.0);
        for (int frequency = 0; frequency < rate / 2; ++frequency) {
            const auto z = std::polar(1.0, -2 * std::numbers::pi * frequency / rate);
            std::complex<double> cascade { 1, 0 };
            for (size_t i = 0; i < filters.size(); ++i) {
                cascade *= filterResponse(filters[i], z);
                peaks[i] = std::max(peaks[i], std::abs(cascade));
            }
        }
        return peaks;
    }
};
} // namespace headsetcontrol
