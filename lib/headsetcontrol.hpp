#pragma once
/**
 * @file headsetcontrol.hpp
 * @brief High-level API for HeadsetControl library
 *
 * This header provides a simple, HID-abstracted interface for controlling
 * USB headsets. Users don't need to deal with HID handles or device paths.
 *
 * Example usage:
 * @code
 * #include <headsetcontrol.hpp>
 *
 * int main() {
 *     // Discover connected headsets
 *     auto headsets = headsetcontrol::discover();
 *
 *     for (auto& headset : headsets) {
 *         std::cout << headset.name() << "\n";
 *
 *         if (headset.supports(CAP_BATTERY_STATUS)) {
 *             auto battery = headset.getBattery();
 *             if (battery) {
 *                 std::cout << "Battery: " << battery->level_percent << "%\n";
 *             }
 *         }
 *
 *         if (headset.supports(CAP_SIDETONE)) {
 *             headset.setSidetone(64);
 *         }
 *     }
 * }
 * @endcode
 */

#include "device.hpp"
#include "result_types.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace headsetcontrol {

// Forward declarations (internal implementation)
class HIDDevice;
class HeadsetImpl;

/**
 * @brief Represents a connected headset with high-level control API
 *
 * This class manages HID connections internally and provides a simple
 * interface for headset control. Connections are established lazily
 * when needed and cached for efficiency.
 *
 * The Headset object is movable but not copyable.
 */
class Headset {
public:
    Headset(Headset&&) noexcept;
    Headset& operator=(Headset&&) noexcept;
    ~Headset();

    // Non-copyable
    Headset(const Headset&)            = delete;
    Headset& operator=(const Headset&) = delete;

    // ========================================================================
    // Device Information
    // ========================================================================

    /**
     * @brief Get the device name
     */
    [[nodiscard]] std::string_view name() const;

    /**
     * @brief Get USB vendor ID
     */
    [[nodiscard]] uint16_t vendorId() const;

    /**
     * @brief Get USB product ID
     */
    [[nodiscard]] uint16_t productId() const;

    /**
     * @brief Check if a capability is supported
     */
    [[nodiscard]] bool supports(enum capabilities cap) const;

    /**
     * @brief Get bitmask of all supported capabilities
     */
    [[nodiscard]] int capabilitiesMask() const;

    /**
     * @brief Get list of supported capability names
     */
    [[nodiscard]] std::vector<std::string_view> capabilityNames() const;

    // ========================================================================
    // Battery & Status
    // ========================================================================

    /**
     * @brief Get battery status
     * @return Battery info or error
     */
    [[nodiscard]] Result<BatteryResult> getBattery();

    /**
     * @brief Get chat-mix level
     * @return Chat-mix info or error
     */
    [[nodiscard]] Result<ChatmixResult> getChatmix();

    // ========================================================================
    // Audio Controls
    // ========================================================================

    /**
     * @brief Set sidetone level
     * @param level Sidetone level (0-128, 0 = off)
     * @return Result info or error
     */
    [[nodiscard]] Result<SidetoneResult> setSidetone(uint8_t level);

    /**
     * @brief Set volume limiter
     * @param enabled Enable/disable volume limiter
     */
    [[nodiscard]] Result<VolumeLimiterResult> setVolumeLimiter(bool enabled);

    // ========================================================================
    // Equalizer
    // ========================================================================

    /**
     * @brief Set equalizer preset
     * @param preset Preset ID
     */
    [[nodiscard]] Result<EqualizerPresetResult> setEqualizerPreset(uint8_t preset);

    /**
     * @brief Set custom equalizer curve
     * @param settings Equalizer band values
     */
    [[nodiscard]] Result<EqualizerResult> setEqualizer(const EqualizerSettings& settings);

    /**
     * @brief Set parametric equalizer
     * @param settings Parametric EQ bands
     */
    [[nodiscard]] Result<ParametricEqualizerResult> setParametricEqualizer(
        const ParametricEqualizerSettings& settings);

    /**
     * @brief Get equalizer info (bands, range, etc.)
     */
    [[nodiscard]] std::optional<EqualizerInfo> getEqualizerInfo() const;

    /**
     * @brief Get parametric equalizer info
     */
    [[nodiscard]] std::optional<ParametricEqualizerInfo> getParametricEqualizerInfo() const;

    /**
     * @brief Get number of equalizer presets
     */
    [[nodiscard]] uint8_t getEqualizerPresetsCount() const;

    // ========================================================================
    // Microphone
    // ========================================================================

    /**
     * @brief Set microphone volume
     * @param volume Volume level (0-128)
     */
    [[nodiscard]] Result<MicVolumeResult> setMicVolume(uint8_t volume);

    /**
     * @brief Set microphone mute LED brightness
     * @param brightness Brightness level (0-3)
     */
    [[nodiscard]] Result<MicMuteLedBrightnessResult> setMicMuteLedBrightness(uint8_t brightness);

    /**
     * @brief Set rotate-to-mute feature
     * @param enabled Enable/disable
     */
    [[nodiscard]] Result<RotateToMuteResult> setRotateToMute(bool enabled);

    // ========================================================================
    // Lights & Audio Cues
    // ========================================================================

    /**
     * @brief Set lights on/off
     * @param enabled Enable/disable lights
     */
    [[nodiscard]] Result<LightsResult> setLights(bool enabled);

    /**
     * @brief Set voice prompts on/off
     * @param enabled Enable/disable voice prompts
     */
    [[nodiscard]] Result<VoicePromptsResult> setVoicePrompts(bool enabled);

    /**
     * @brief Play notification sound
     * @param soundId Sound ID to play
     */
    [[nodiscard]] Result<NotificationSoundResult> playNotificationSound(uint8_t soundId);

    // ========================================================================
    // Power & Bluetooth
    // ========================================================================

    /**
     * @brief Set inactive time (auto power-off)
     * @param minutes Minutes until power-off (0 = disabled)
     */
    [[nodiscard]] Result<InactiveTimeResult> setInactiveTime(uint8_t minutes);

    /**
     * @brief Set Bluetooth when powered on
     * @param enabled Enable/disable Bluetooth at power-on
     */
    [[nodiscard]] Result<BluetoothWhenPoweredOnResult> setBluetoothWhenPoweredOn(bool enabled);

    /**
     * @brief Set Bluetooth call volume
     * @param volume Volume level
     */
    [[nodiscard]] Result<BluetoothCallVolumeResult> setBluetoothCallVolume(uint8_t volume);

private:
    friend class HeadsetImpl;
    friend std::vector<Headset> discover();
    friend std::vector<Headset> discoverAll();

    // Private constructor - use discover() to create instances
    explicit Headset(std::unique_ptr<HeadsetImpl> impl);

    std::unique_ptr<HeadsetImpl> impl_;
};

// ============================================================================
// Discovery Functions
// ============================================================================

/**
 * @brief Discover connected headsets
 *
 * Scans USB devices and returns a list of supported headsets.
 * The library is automatically initialized on first call.
 *
 * @return Vector of discovered headsets
 *
 * @code
 * auto headsets = headsetcontrol::discover();
 * if (headsets.empty()) {
 *     std::cout << "No supported headsets found\n";
 * }
 * @endcode
 */
[[nodiscard]] std::vector<Headset> discover();

/**
 * @brief Discover all supported headsets including duplicates
 *
 * Unlike discover(), this returns all matching USB devices even if
 * multiple devices of the same model are connected.
 *
 * @return Vector of all discovered headsets
 */
[[nodiscard]] std::vector<Headset> discoverAll();

/**
 * @brief Get library version string
 */
[[nodiscard]] std::string_view version();

/**
 * @brief Get list of all supported device names
 *
 * Returns names of all devices the library can control,
 * regardless of whether they're currently connected.
 */
[[nodiscard]] std::vector<std::string_view> supportedDevices();

/**
 * @brief Enable or disable test device mode
 *
 * When enabled, discover() and discoverAll() will include the
 * HeadsetControl Test device (0xF00B:0xA00C) even when no physical
 * device is connected. This is useful for testing and development.
 *
 * @param enable true to enable test device, false to disable
 *
 * @code
 * headsetcontrol::enableTestDevice(true);
 * auto headsets = headsetcontrol::discover();
 * // headsets will include the test device
 * @endcode
 */
void enableTestDevice(bool enable = true);

/**
 * @brief Check if test device mode is enabled
 *
 * @return true if test device mode is enabled
 */
[[nodiscard]] bool isTestDeviceEnabled();

// ============================================================================
// Configuration Functions
// ============================================================================

/**
 * @brief Set the HID device timeout
 *
 * Controls how long HID read operations wait for a response.
 * Default is 5000ms (5 seconds).
 *
 * @param timeout_ms Timeout in milliseconds
 */
void setDeviceTimeout(int timeout_ms);

/**
 * @brief Get the current HID device timeout
 *
 * @return Timeout in milliseconds
 */
[[nodiscard]] int getDeviceTimeout();

/**
 * @brief Set the test profile for the test device
 *
 * Controls the behavior of the HeadsetControl Test device.
 * Only useful when test device mode is enabled.
 *
 * Profiles:
 * - 0: Normal operation (default)
 * - 1: Simulate error conditions
 * - 2: Simulate charging state
 * - 3: Basic battery info only (no extended info)
 * - 5: Simulate timeout errors
 * - 10: Minimal capabilities
 *
 * @param profile Profile number
 */
void setTestProfile(int profile);

/**
 * @brief Get the current test profile
 *
 * @return Current test profile number
 */
[[nodiscard]] int getTestProfile();

} // namespace headsetcontrol
