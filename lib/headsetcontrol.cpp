#include "headsetcontrol.hpp"

#include "device_registry.hpp"
#include "devices/hid_device.hpp"
#include "hid_utility.hpp"
#include "version.h"

#include <hidapi.h>

#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace headsetcontrol {

// ============================================================================
// Library Initialization (singleton)
// ============================================================================

namespace {

    // Test device vendor/product IDs (using different names to avoid macro conflicts)
    constexpr uint16_t kTestDeviceVendor  = 0xF00B;
    constexpr uint16_t kTestDeviceProduct = 0xA00C;

    class LibraryState {
    public:
        static LibraryState& instance()
        {
            static LibraryState state;
            return state;
        }

        void ensureInitialized()
        {
            std::call_once(init_flag_, [this]() {
                hid_init();
                DeviceRegistry::instance().initialize();
                initialized_ = true;
            });
        }

        ~LibraryState()
        {
            if (initialized_) {
                hid_exit();
            }
        }

        void setTestDeviceEnabled(bool enabled) { test_device_enabled_ = enabled; }
        bool isTestDeviceEnabled() const { return test_device_enabled_; }

    private:
        LibraryState() = default;
        std::once_flag init_flag_;
        bool initialized_         = false;
        bool test_device_enabled_ = false;
    };

} // namespace

// ============================================================================
// HeadsetImpl - Internal implementation
// ============================================================================

class HeadsetImpl {
public:
    HeadsetImpl(HIDDevice* device, uint16_t product_id, bool is_test_device = false)
        : device_(device)
        , product_id_(product_id)
        , is_test_device_(is_test_device)
    {
    }

    ~HeadsetImpl()
    {
        closeAllConnections();
    }

    // Non-copyable, non-movable (prevent dangling handles)
    HeadsetImpl(const HeadsetImpl&)            = delete;
    HeadsetImpl& operator=(const HeadsetImpl&) = delete;
    HeadsetImpl(HeadsetImpl&&)                 = delete;
    HeadsetImpl& operator=(HeadsetImpl&&)      = delete;

    HIDDevice* device() const { return device_; }
    uint16_t productId() const { return product_id_; }
    bool isTestDevice() const { return is_test_device_; }

    /**
     * @brief Get or open HID connection for a capability
     *
     * Connections are cached per capability detail (interface/usage).
     * Returns nullptr if connection fails or if this is a test device.
     * Test devices don't need real HID connections.
     */
    hid_device* getConnection(capabilities cap)
    {
        // Test devices don't need real HID connections
        if (is_test_device_) {
            return nullptr;
        }

        auto detail = device_->getCapabilityDetail(cap);

        // Create cache key from connection parameters
        // Cast interface to uint32_t first to avoid UB with negative values
        uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(detail.interface)) << 32)
            | (static_cast<uint64_t>(detail.usagepage) << 16)
            | detail.usageid;

        // Check cache
        auto it = connections_.find(key);
        if (it != connections_.end()) {
            return it->second;
        }

        // Open new connection
        auto path = get_hid_path(device_->getVendorId(), product_id_,
            detail.interface, detail.usagepage, detail.usageid);

        if (!path) {
            return nullptr;
        }

        hid_device* handle = hid_open_path(path->c_str());
        if (handle) {
            connections_[key] = handle;
        }

        return handle;
    }

private:
    void closeAllConnections()
    {
        for (auto& [key, handle] : connections_) {
            if (handle) {
                hid_close(handle);
            }
        }
        connections_.clear();
    }

    HIDDevice* device_;
    uint16_t product_id_;
    bool is_test_device_;
    std::unordered_map<uint64_t, hid_device*> connections_;
};

// ============================================================================
// Headset Implementation
// ============================================================================

Headset::Headset(std::unique_ptr<HeadsetImpl> impl)
    : impl_(std::move(impl))
{
}

Headset::Headset(Headset&&) noexcept            = default;
Headset& Headset::operator=(Headset&&) noexcept = default;
Headset::~Headset()                             = default;

std::string_view Headset::name() const
{
    return impl_->device()->getDeviceName();
}

uint16_t Headset::vendorId() const
{
    return impl_->device()->getVendorId();
}

uint16_t Headset::productId() const
{
    return impl_->productId();
}

bool Headset::supports(enum capabilities cap) const
{
    return (impl_->device()->getCapabilities() & B(cap)) != 0;
}

int Headset::capabilitiesMask() const
{
    return impl_->device()->getCapabilities();
}

std::vector<std::string_view> Headset::capabilityNames() const
{
    std::vector<std::string_view> names;
    int caps = impl_->device()->getCapabilities();

    for (int i = 0; i < NUM_CAPABILITIES; i++) {
        if (caps & B(i)) {
            names.push_back(capabilities_str[i]);
        }
    }

    return names;
}

// ============================================================================
// Feature Implementations (with automatic connection handling)
// ============================================================================

// Helper macro for feature implementations with automatic connection handling
// Note: Using __VA_OPT__ (C++20) instead of ##__VA_ARGS__ (GNU extension)
// Test devices are allowed to have nullptr handles since they don't use real HID.
#define HEADSET_FEATURE_IMPL(cap, method, ...)                             \
    do {                                                                   \
        if (!supports(cap)) {                                              \
            return DeviceError::notSupported("Feature not supported");     \
        }                                                                  \
        hid_device* handle = impl_->getConnection(cap);                    \
        if (!handle && !impl_->isTestDevice()) {                           \
            return DeviceError::hidError("Could not open device");         \
        }                                                                  \
        return impl_->device()->method(handle __VA_OPT__(, ) __VA_ARGS__); \
    } while (0)

Result<BatteryResult> Headset::getBattery()
{
    HEADSET_FEATURE_IMPL(CAP_BATTERY_STATUS, getBattery);
}

Result<ChatmixResult> Headset::getChatmix()
{
    HEADSET_FEATURE_IMPL(CAP_CHATMIX_STATUS, getChatmix);
}

Result<SidetoneResult> Headset::setSidetone(uint8_t level)
{
    HEADSET_FEATURE_IMPL(CAP_SIDETONE, setSidetone, level);
}

Result<VolumeLimiterResult> Headset::setVolumeLimiter(bool enabled)
{
    HEADSET_FEATURE_IMPL(CAP_VOLUME_LIMITER, setVolumeLimiter, enabled);
}

Result<EqualizerPresetResult> Headset::setEqualizerPreset(uint8_t preset)
{
    HEADSET_FEATURE_IMPL(CAP_EQUALIZER_PRESET, setEqualizerPreset, preset);
}

Result<EqualizerResult> Headset::setEqualizer(const EqualizerSettings& settings)
{
    HEADSET_FEATURE_IMPL(CAP_EQUALIZER, setEqualizer, settings);
}

Result<ParametricEqualizerResult> Headset::setParametricEqualizer(
    const ParametricEqualizerSettings& settings)
{
    HEADSET_FEATURE_IMPL(CAP_PARAMETRIC_EQUALIZER, setParametricEqualizer, settings);
}

std::optional<EqualizerInfo> Headset::getEqualizerInfo() const
{
    return impl_->device()->getEqualizerInfo();
}

std::optional<ParametricEqualizerInfo> Headset::getParametricEqualizerInfo() const
{
    return impl_->device()->getParametricEqualizerInfo();
}

uint8_t Headset::getEqualizerPresetsCount() const
{
    return impl_->device()->getEqualizerPresetsCount();
}

Result<MicVolumeResult> Headset::setMicVolume(uint8_t volume)
{
    HEADSET_FEATURE_IMPL(CAP_MICROPHONE_VOLUME, setMicVolume, volume);
}

Result<MicMuteLedBrightnessResult> Headset::setMicMuteLedBrightness(uint8_t brightness)
{
    HEADSET_FEATURE_IMPL(CAP_MICROPHONE_MUTE_LED_BRIGHTNESS, setMicMuteLedBrightness, brightness);
}

Result<RotateToMuteResult> Headset::setRotateToMute(bool enabled)
{
    HEADSET_FEATURE_IMPL(CAP_ROTATE_TO_MUTE, setRotateToMute, enabled);
}

Result<LightsResult> Headset::setLights(bool enabled)
{
    HEADSET_FEATURE_IMPL(CAP_LIGHTS, setLights, enabled);
}

Result<VoicePromptsResult> Headset::setVoicePrompts(bool enabled)
{
    HEADSET_FEATURE_IMPL(CAP_VOICE_PROMPTS, setVoicePrompts, enabled);
}

Result<NotificationSoundResult> Headset::playNotificationSound(uint8_t soundId)
{
    HEADSET_FEATURE_IMPL(CAP_NOTIFICATION_SOUND, notificationSound, soundId);
}

Result<InactiveTimeResult> Headset::setInactiveTime(uint8_t minutes)
{
    HEADSET_FEATURE_IMPL(CAP_INACTIVE_TIME, setInactiveTime, minutes);
}

Result<BluetoothWhenPoweredOnResult> Headset::setBluetoothWhenPoweredOn(bool enabled)
{
    HEADSET_FEATURE_IMPL(CAP_BT_WHEN_POWERED_ON, setBluetoothWhenPoweredOn, enabled);
}

Result<BluetoothCallVolumeResult> Headset::setBluetoothCallVolume(uint8_t volume)
{
    HEADSET_FEATURE_IMPL(CAP_BT_CALL_VOLUME, setBluetoothCallVolume, volume);
}

#undef HEADSET_FEATURE_IMPL

// ============================================================================
// Discovery Functions
// ============================================================================

namespace {

    /**
     * @brief RAII wrapper for hid_enumerate() result
     *
     * Ensures hid_free_enumeration() is called even if an exception is thrown.
     */
    class HIDEnumerationGuard {
    public:
        explicit HIDEnumerationGuard(hid_device_info* devices)
            : devices_(devices)
        {
        }

        ~HIDEnumerationGuard()
        {
            if (devices_) {
                hid_free_enumeration(devices_);
            }
        }

        // Non-copyable, non-movable
        HIDEnumerationGuard(const HIDEnumerationGuard&)            = delete;
        HIDEnumerationGuard& operator=(const HIDEnumerationGuard&) = delete;

        [[nodiscard]] hid_device_info* get() const { return devices_; }

    private:
        hid_device_info* devices_;
    };

} // namespace

std::vector<Headset> discover()
{
    LibraryState::instance().ensureInitialized();

    std::vector<Headset> headsets;
    auto& registry = DeviceRegistry::instance();

    // Track which device types we've already found (by vendor:product)
    std::vector<std::pair<uint16_t, uint16_t>> found_devices;

    // Add test device if enabled
    if (LibraryState::instance().isTestDeviceEnabled()) {
        if (auto* test_dev = registry.getDevice(kTestDeviceVendor, kTestDeviceProduct)) {
            found_devices.emplace_back(kTestDeviceVendor, kTestDeviceProduct);
            headsets.push_back(Headset(
                std::make_unique<HeadsetImpl>(test_dev, kTestDeviceProduct, true)));
        }
    }

    // RAII guard ensures hid_free_enumeration is called even on exception
    HIDEnumerationGuard guard(hid_enumerate(0, 0));

    for (auto* cur = guard.get(); cur; cur = cur->next) {
        // Check if we already found this device type
        auto key = std::make_pair(cur->vendor_id, cur->product_id);
        if (std::find(found_devices.begin(), found_devices.end(), key) != found_devices.end()) {
            continue;
        }

        // Check if device is supported
        HIDDevice* device = registry.getDevice(cur->vendor_id, cur->product_id);
        if (device) {
            found_devices.push_back(key);
            headsets.push_back(Headset(
                std::make_unique<HeadsetImpl>(device, cur->product_id)));
        }
    }

    return headsets;
}

std::vector<Headset> discoverAll()
{
    LibraryState::instance().ensureInitialized();

    std::vector<Headset> headsets;
    auto& registry = DeviceRegistry::instance();

    // Add test device if enabled
    if (LibraryState::instance().isTestDeviceEnabled()) {
        if (auto* test_dev = registry.getDevice(kTestDeviceVendor, kTestDeviceProduct)) {
            headsets.push_back(Headset(
                std::make_unique<HeadsetImpl>(test_dev, kTestDeviceProduct, true)));
        }
    }

    // RAII guard ensures hid_free_enumeration is called even on exception
    HIDEnumerationGuard guard(hid_enumerate(0, 0));

    for (auto* cur = guard.get(); cur; cur = cur->next) {
        HIDDevice* device = registry.getDevice(cur->vendor_id, cur->product_id);
        if (device) {
            headsets.push_back(Headset(
                std::make_unique<HeadsetImpl>(device, cur->product_id)));
        }
    }

    return headsets;
}

std::string_view version()
{
    return VERSION;
}

std::vector<std::string_view> supportedDevices()
{
    LibraryState::instance().ensureInitialized();

    std::vector<std::string_view> names;
    for (const auto& device : DeviceRegistry::instance().getAllDevices()) {
        names.push_back(device->getDeviceName());
    }
    return names;
}

void enableTestDevice(bool enable)
{
    LibraryState::instance().ensureInitialized();
    LibraryState::instance().setTestDeviceEnabled(enable);
}

bool isTestDeviceEnabled()
{
    return LibraryState::instance().isTestDeviceEnabled();
}

} // namespace headsetcontrol

// External globals defined in globals.cpp
extern int hsc_device_timeout;
extern int test_profile;

namespace headsetcontrol {

void setDeviceTimeout(int timeout_ms)
{
    hsc_device_timeout = timeout_ms;
}

int getDeviceTimeout()
{
    return hsc_device_timeout;
}

void setTestProfile(int profile)
{
    test_profile = profile;
}

int getTestProfile()
{
    return test_profile;
}

} // namespace headsetcontrol
