#pragma once

#include "protocols/logitech_centurion_protocol.hpp"
#include <array>
#include <chrono>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Logitech ASTRO A20 X wireless transmitter (046d:0b35, "A20 X HID").
 *
 * Verified on Windows with the headset connected wirelessly: Centurion frames
 * use prefix 50 23 on interface 5 (usage page 0xffa0). Like the G522 and the
 * G PRO X 3, the transmitter exposes the bridge feature (index 3) and the
 * battery feature 0x0104 lives on the headset behind it.
 */
class LogitechAstroA20XWireless : public protocols::LogitechCenturionProtocol {
public:
    static constexpr std::array<uint16_t, 1> SUPPORTED_PRODUCT_IDS { 0x0b35 };
    static constexpr std::array<uint8_t, 2> FRAME_PREFIX { 0x50, 0x23 };

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
        return "Logitech ASTRO A20 X"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_BATTERY_STATUS);
    }

    constexpr capability_detail getCapabilityDetail(enum capabilities cap) const override
    {
        switch (cap) {
        case CAP_BATTERY_STATUS:
            return { .usagepage = 0xffa0, .usageid = 0x0001, .interface_id = 5 };
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
};

/**
 * @brief Logitech ASTRO A20 X connected over USB (046d:0b2e, "A20 X - USB wired mode").
 *
 * Verified on Windows: Centurion frames use prefix 50 23 on interface 3
 * (usage page 0xffa0). Unlike the wireless transmitter there is no bridge
 * feature (Root.getFeature(0x0003) is rejected with error 0x05): the battery
 * feature 0x0104 answers directly on the root device.
 */
class LogitechAstroA20X : public protocols::LogitechCenturionProtocol {
public:
    static constexpr std::array<uint16_t, 1> SUPPORTED_PRODUCT_IDS { 0x0b2e };
    static constexpr std::array<uint8_t, 2> FRAME_PREFIX { 0x50, 0x23 };

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
        return "Logitech ASTRO A20 X (USB wired)"sv;
    }

    constexpr int getCapabilities() const override
    {
        return B(CAP_BATTERY_STATUS);
    }

    constexpr capability_detail getCapabilityDetail(enum capabilities cap) const override
    {
        switch (cap) {
        case CAP_BATTERY_STATUS:
            return { .usagepage = 0xffa0, .usageid = 0x0001, .interface_id = 3 };
        default:
            return HIDDevice::getCapabilityDetail(cap);
        }
    }

    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        auto start_time = std::chrono::steady_clock::now();

        if (!battery_index_) {
            auto index = lookupDirectFeature(
                device_handle,
                static_cast<uint16_t>(protocols::CenturionFeature::CenturionBatterySoc));
            if (!index) {
                return index.error();
            }
            battery_index_ = *index;
        }

        auto battery = sendCenturionRequest(device_handle, *battery_index_, 0x00, {}, true);
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

protected:
    std::span<const uint8_t> centurionFramePrefix() const override
    {
        return FRAME_PREFIX;
    }

    protocols::CenturionOptions centurionOptions() const override
    {
        return {
            .exact_direct_length = true,
        };
    }

private:
    /**
     * @brief Find a feature index with a direct (non-bridged) Root.getFeature.
     *
     * Root.getFeature replies [index, type, version]; index 0 means the
     * feature does not exist.
     */
    [[nodiscard]] Result<uint8_t> lookupDirectFeature(hid_device* device_handle, uint16_t feature_id) const
    {
        auto lookup = sendCenturionRequest(
            device_handle,
            static_cast<uint8_t>(protocols::CenturionFeature::Root),
            0x00,
            std::array<uint8_t, 2> { static_cast<uint8_t>(feature_id >> 8), static_cast<uint8_t>(feature_id & 0xFF) },
            true);
        if (!lookup) {
            return lookup.error();
        }
        if (lookup->empty() || (*lookup)[0] == 0) {
            return DeviceError::notSupported("Centurion feature not available on this device");
        }

        return (*lookup)[0];
    }

    mutable std::optional<uint8_t> battery_index_;
};

} // namespace headsetcontrol
