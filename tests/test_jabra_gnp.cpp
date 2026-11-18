#include "device_registry.hpp"
#include "devices/jabra_generic.hpp"

#include <cstdint>
#include <deque>
#include <iostream>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace headsetcontrol::testing {

class JabraTestFailure : public std::runtime_error {
public:
    explicit JabraTestFailure(const std::string& message)
        : std::runtime_error(message)
    {
    }
};

#define JABRA_ASSERT(condition, message)                                           \
    do {                                                                           \
        if (!(condition))                                                          \
            throw JabraTestFailure(std::string("Assertion failed: ") + (message)); \
    } while (false)

using protocols::JabraGNPBase;

// Answers GNP requests per destination address: a name, a NACK reason, or silence
class JabraMockHID final : public HIDInterface {
public:
    struct Peer {
        std::string name; // answered to IDENT name when non-empty
        uint8_t nack = 0xF5; // otherwise NACKed with this reason; 0 = no reply at all
        // GET payloads by (cmd, sub); a SET to one of them is stored and ACKed
        std::map<std::pair<uint8_t, uint8_t>, std::vector<uint8_t>> values;
    };

    std::map<uint8_t, Peer> peers;
    std::vector<std::vector<uint8_t>> writes;
    std::deque<std::vector<uint8_t>> pending;
    std::vector<std::vector<uint8_t>> inject; // queued in front of the next reply

    static std::vector<uint8_t> frame(uint8_t src, uint8_t seq, uint8_t cmd, uint8_t sub, std::vector<uint8_t> payload,
        uint8_t dst = 0x00, uint8_t type = 3)
    {
        std::vector<uint8_t> f(JabraGNPBase::MSG_SIZE, 0);
        f[0]                     = JabraGNPBase::REPORT_ID;
        f[JabraGNPBase::OFF_DST] = dst;
        f[JabraGNPBase::OFF_SRC] = src;
        f[JabraGNPBase::OFF_SEQ] = seq;
        f[JabraGNPBase::OFF_LEN] = static_cast<uint8_t>((type << 6) | (6 + payload.size()));
        f[JabraGNPBase::OFF_CMD] = cmd;
        f[JabraGNPBase::OFF_SUB] = sub;
        std::copy(payload.begin(), payload.end(), f.begin() + JabraGNPBase::OFF_PAYLOAD);
        return f;
    }

    Result<void> write(hid_device*, std::span<const uint8_t> data) override
    {
        writes.emplace_back(data.begin(), data.end());
        const uint8_t dst = data[JabraGNPBase::OFF_DST];
        const uint8_t seq = data[JabraGNPBase::OFF_SEQ];
        auto it           = peers.find(dst);
        if (it == peers.end()) {
            return {}; // nothing on that address answers
        }

        for (auto& stray : inject) {
            pending.push_back(std::move(stray));
        }
        inject.clear();

        const uint8_t cmd  = data[JabraGNPBase::OFF_CMD];
        const uint8_t sub  = data[JabraGNPBase::OFF_SUB];
        const uint8_t type = data[JabraGNPBase::OFF_LEN] >> 6;
        const size_t len   = data[JabraGNPBase::OFF_LEN] & 0x3F;
        auto& peer         = it->second;
        auto value         = peer.values.find({ cmd, sub });

        if (cmd == 0x02 && sub == 0x00 && !peer.name.empty()) {
            std::vector<uint8_t> payload { static_cast<uint8_t>(peer.name.size()) };
            payload.insert(payload.end(), peer.name.begin(), peer.name.end());
            pending.push_back(frame(dst, seq, cmd, sub, payload));
        } else if (value != peer.values.end() && type == 1) {
            pending.push_back(frame(dst, seq, cmd, sub, value->second));
        } else if (value != peer.values.end() && type == 2) {
            value->second.assign(data.begin() + JabraGNPBase::OFF_PAYLOAD, data.begin() + 1 + len);
            pending.push_back(frame(dst, seq, 0xFF, 0x00, {}));
        } else if (peer.nack != 0) {
            pending.push_back(frame(dst, seq, 0xFE, peer.nack, {}));
        }
        return {};
    }

    Result<void> write(hid_device* handle, std::span<const uint8_t> data, size_t size) override
    {
        return write(handle, data.first(std::min(size, data.size())));
    }

    Result<size_t> readTimeout(hid_device*, std::span<uint8_t> data, int) override
    {
        if (pending.empty())
            return DeviceError::timeout("Simulated timeout");
        auto reply = std::move(pending.front());
        pending.pop_front();
        const size_t size = std::min(reply.size(), data.size());
        std::copy_n(reply.begin(), size, data.begin());
        return size;
    }

    Result<void> sendFeatureReport(hid_device*, std::span<const uint8_t>) override { return DeviceError::notSupported("unused"); }
    Result<void> sendFeatureReport(hid_device*, std::span<const uint8_t>, size_t) override { return DeviceError::notSupported("unused"); }
    Result<size_t> getFeatureReport(hid_device*, std::span<uint8_t>) override { return DeviceError::notSupported("unused"); }
    Result<size_t> getInputReport(hid_device*, std::span<uint8_t>) override { return DeviceError::notSupported("unused"); }

    std::vector<uint8_t> addressesWritten() const
    {
        std::vector<uint8_t> out;
        for (const auto& w : writes)
            out.push_back(w[JabraGNPBase::OFF_DST]);
        return out;
    }
};

class TestableJabraGeneric final : public JabraGeneric {
public:
    explicit TestableJabraGeneric(JabraMockHID& hid, bool dongle = false)
        : hid_(hid)
        , dongle_(dongle)
    {
    }

protected:
    HIDInterface& getHIDInterface() const override { return hid_; }
    bool looksLikeDongle(hid_device*) const override { return dongle_; }

private:
    JabraMockHID& hid_;
    bool dongle_;
};

auto* const FAKE_HANDLE = reinterpret_cast<hid_device*>(0x1);

void testUsbHeadsetProbedAt08()
{
    JabraMockHID hid;
    hid.peers[0x08] = { .name = "Jabra Evolve2 85" };
    TestableJabraGeneric device(hid);

    auto name = device.getString(FAKE_HANDLE, 0x02, 0x00);
    JABRA_ASSERT(name && *name == "Jabra Evolve2 85", "name read from the USB headset");
    // probe + the request itself, both at 0x08
    JABRA_ASSERT((hid.addressesWritten() == std::vector<uint8_t> { 0x08, 0x08 }), "USB headset answers at 0x08");

    hid.writes.clear();
    (void)device.getString(FAKE_HANDLE, 0x02, 0x00);
    JABRA_ASSERT((hid.addressesWritten() == std::vector<uint8_t> { 0x08 }), "address is cached for the handle");

    device.onConnectionClosed(FAKE_HANDLE);
    hid.writes.clear();
    (void)device.getString(FAKE_HANDLE, 0x02, 0x00);
    JABRA_ASSERT(hid.addressesWritten().size() == 2, "closing the handle forgets the address");
}

void testDongleFallsBackTo04()
{
    JabraMockHID hid;
    hid.peers[0x08] = { .nack = 0xF5 }; // dongle: no device at 0x08
    hid.peers[0x04] = { .name = "Jabra Evolve2 65" };
    TestableJabraGeneric device(hid);

    auto name = device.getString(FAKE_HANDLE, 0x02, 0x00);
    JABRA_ASSERT(name && *name == "Jabra Evolve2 65", "name read from the paired headset");
    JABRA_ASSERT((hid.addressesWritten() == std::vector<uint8_t> { 0x08, 0x04, 0x04 }), "0x08 rejected, 0x04 used");
}

void testDongleProbedFirstByName()
{
    JabraMockHID hid;
    hid.peers[0x04] = { .name = "Jabra Evolve2 75" };
    TestableJabraGeneric device(hid, true);

    auto name = device.getString(FAKE_HANDLE, 0x02, 0x00);
    JABRA_ASSERT(name.hasValue(), "paired headset answers");
    JABRA_ASSERT((hid.addressesWritten() == std::vector<uint8_t> { 0x04, 0x04 }), "a Link is probed at 0x04 first");
}

void testSilentAddressSkipped()
{
    JabraMockHID hid;
    hid.peers[0x04] = { .name = "Jabra Evolve2 65" }; // nothing at all answers at 0x08
    TestableJabraGeneric device(hid);

    auto name = device.getString(FAKE_HANDLE, 0x02, 0x00);
    JABRA_ASSERT(name.hasValue(), "timeout at 0x08 falls through to 0x04");
}

void testDongleWithoutHeadset()
{
    JabraMockHID hid;
    hid.peers[0x08] = { .nack = 0xF5 };
    hid.peers[0x04] = { .nack = 0xF3 }; // peer offline
    TestableJabraGeneric device(hid);

    auto battery = device.getBattery(FAKE_HANDLE);
    JABRA_ASSERT(!battery && battery.error().code == DeviceError::Code::DeviceOffline, "headset off is reported offline");
    JABRA_ASSERT(battery.error().details.find("dongle") != std::string::npos, "details name the dongle");

    // Once the headset comes on, it is found without restarting
    hid.peers[0x04] = { .name = "Jabra Evolve2 65" };
    auto name       = device.getString(FAKE_HANDLE, 0x02, 0x00);
    JABRA_ASSERT(name.hasValue(), "failed probe is not cached");
}

void testStrayRepliesSkipped()
{
    JabraMockHID hid;
    hid.peers[0x08]                        = { .name = "Jabra Evolve2 85" };
    hid.peers[0x08].values[{ 0x12, 0x02 }] = { 0x00, 0x50 };
    TestableJabraGeneric device(hid);
    (void)device.getString(FAKE_HANDLE, 0x02, 0x00); // probe

    // Same sequence number as the next request, but not its reply
    const uint8_t next_seq = static_cast<uint8_t>(hid.writes.back()[JabraGNPBase::OFF_SEQ] + 1);
    hid.inject             = {
        JabraMockHID::frame(0x04, next_seq, 0x12, 0x02, { 0x00, 0x10 }), // other address
        JabraMockHID::frame(0x08, next_seq, 0x13, 0x7C, { 0x00, 0x00 }), // other command
        JabraMockHID::frame(0x08, next_seq, 0x12, 0x02, { 0x00, 0x20 }, 0x01), // not for the host
        JabraMockHID::frame(0x08, 0x00, 0x12, 0x02, { 0x00, 0x30 }, 0x00, 0), // event
    };
    auto battery = device.getBattery(FAKE_HANDLE);
    JABRA_ASSERT(battery && battery->level_percent == 80, "only the matching reply is used");
}

void testPaddedStringReply()
{
    JabraMockHID hid;
    hid.peers[0x08] = { .name = "Jabra Evolve2 85" };
    TestableJabraGeneric device(hid);
    (void)device.getString(FAKE_HANDLE, 0x02, 0x00); // probe

    // Length byte 3, payload padded to 6 bytes
    hid.peers[0x08].values[{ 0x02, 0x01 }] = { 0x03, 'A', 'B', 'C', 'x', 'y' };
    auto serial                            = device.getString(FAKE_HANDLE, 0x02, 0x01);
    JABRA_ASSERT(serial && *serial == "ABC", "string length comes from its length byte");

    hid.peers[0x08].values[{ 0x02, 0x01 }] = { 0x09, 'A' };
    JABRA_ASSERT(!device.getString(FAKE_HANDLE, 0x02, 0x01), "length beyond the payload is rejected");
}

void testBatteryAbove100Rejected()
{
    JabraMockHID hid;
    hid.peers[0x08]                        = { .name = "Jabra Evolve2 85" };
    hid.peers[0x08].values[{ 0x12, 0x02 }] = { 0x01, 0xC8 };
    TestableJabraGeneric device(hid);

    auto battery = device.getBattery(FAKE_HANDLE);
    JABRA_ASSERT(!battery && battery.error().code == DeviceError::Code::ProtocolError, "200 % is bad data");

    hid.peers[0x08].values[{ 0x12, 0x02 }] = { 0x01, 0x64 };
    battery                                = device.getBattery(FAKE_HANDLE);
    JABRA_ASSERT(battery && battery->level_percent == 100 && battery->status == BATTERY_CHARGING, "100 % charging");
}

void testSidetoneLevels()
{
    JabraMockHID hid;
    hid.peers[0x08] = { .name = "Jabra Evolve2 85" };
    auto& sidetone  = hid.peers[0x08].values[{ 0x13, 0x7C }];
    sidetone        = { 0x00, static_cast<uint8_t>(int8_t { -4 }) };
    TestableJabraGeneric device(hid);

    auto state = device.getSidetone(FAKE_HANDLE);
    JABRA_ASSERT(state && state->level_name == "-4 dB", "a -4 dB catalog level is read");
    JABRA_ASSERT(state->device_level == 2, "shown as the nearest written step, -3 dB");

    auto muted = device.setSidetone(FAKE_HANDLE, 0);
    JABRA_ASSERT(muted && muted->is_muted, "muting works");
    JABRA_ASSERT((sidetone == std::vector<uint8_t> { 0x01, static_cast<uint8_t>(int8_t { -4 }) }), "muting keeps the level byte");

    auto zero = device.setSidetone(FAKE_HANDLE, 73);
    JABRA_ASSERT(zero && zero->level_name == "0 dB", "0 dB has no sign");

    auto max = device.setSidetone(FAKE_HANDLE, 128);
    JABRA_ASSERT(max && max->level_name == "+6 dB", "max level");
    JABRA_ASSERT((sidetone == std::vector<uint8_t> { 0x00, 0x06 }), "+6 dB written unmuted");

    sidetone = { 0x00, 0x05 };
    JABRA_ASSERT(!device.getSidetone(FAKE_HANDLE), "unknown dB value is rejected");
}

void testInactivityOfferedValues()
{
    JabraMockHID hid;
    hid.peers[0x08] = { .name = "Jabra Evolve2 85" };
    auto& timer     = hid.peers[0x08].values[{ 0x13, 0x90 }];
    timer           = { 0x00 };
    TestableJabraGeneric device(hid);

    struct Case {
        uint8_t asked, minutes, units;
    };
    for (auto [asked, minutes, units] : { Case { 0, 0, 0 }, Case { 7, 0, 0 }, Case { 10, 15, 3 }, Case { 45, 30, 6 },
             Case { 50, 60, 12 }, Case { 90, 60, 12 }, Case { 200, 240, 48 }, Case { 255, 240, 48 } }) {
        auto result = device.setInactiveTime(FAKE_HANDLE, asked);
        JABRA_ASSERT(result && result->minutes == minutes, "nearest offered timer value");
        JABRA_ASSERT((timer == std::vector<uint8_t> { units }), "written in units of 5 minutes");
        JABRA_ASSERT(result->max_minutes == 240, "longest offered value");
    }
}

hid_device_info makeInfo(uint16_t pid, uint16_t usage_page, uint16_t usage, uint16_t vid = 0x0B0E)
{
    hid_device_info info {};
    info.vendor_id  = vid;
    info.product_id = pid;
    info.usage_page = usage_page;
    info.usage      = usage;
    return info;
}

void testRegistryMatchesGnpCollection()
{
    auto& registry = DeviceRegistry::instance();
    registry.initialize();

    // Any Jabra product with a GNP collection, whether or not it was ever seen here
    auto gnp    = makeInfo(0x0E41, 0xFF00, 0x01); // Evolve2 40
    auto* jabra = registry.findDevice(gnp);
    JABRA_ASSERT(jabra != nullptr && std::string(jabra->getDeviceName()) == "Jabra (GN Protocol)", "Jabra GNP device is found");
    JABRA_ASSERT(jabra->getMatchedProductId() == 0x0E41, "matched product ID is recorded");
    JABRA_ASSERT(jabra->getProductIds().empty(), "matched by collection, not by product ID");
    JABRA_ASSERT(jabra->getSupportedPlatforms() == PLATFORM_ALL, "all platforms");
    JABRA_ASSERT(registry.getDevice(0x0B0E, 0x0E41) == nullptr, "a product ID lookup alone does not claim it");

    auto gnp5 = makeInfo(0x0E41, 0xFF00, 0x05);
    JABRA_ASSERT(registry.findDevice(gnp5) == jabra, "usage 0x05 is a GNP collection too");

    auto telephony = makeInfo(0x0E41, 0x000B, 0x05);
    JABRA_ASSERT(registry.findDevice(telephony) == nullptr, "other collections are not claimed");

    // DECT bases answer at 8 too, as the base: left out, while their USB headset is kept
    JABRA_ASSERT(registry.findDevice(makeInfo(0x1112, 0xFF00, 0x01)) == nullptr, "Engage 75 base is not claimed");
    JABRA_ASSERT(registry.findDevice(makeInfo(0x1131, 0xFF00, 0x01)) == nullptr, "Link 400 is not claimed");
    JABRA_ASSERT(registry.findDevice(makeInfo(0x1116, 0xFF00, 0x01)) == jabra, "Engage headset on USB is claimed");

    auto other_vendor = makeInfo(0x0E41, 0xFF00, 0x01, 0x1234);
    JABRA_ASSERT(registry.findDevice(other_vendor) == nullptr, "other vendors are not claimed");

    auto caps = jabra->getCapabilities();
    for (auto cap : { CAP_SIDETONE, CAP_SIDETONE_STATUS, CAP_BATTERY_STATUS, CAP_LIGHTS, CAP_VOICE_PROMPTS,
             CAP_INACTIVE_TIME, CAP_VOLUME_LIMITER }) {
        JABRA_ASSERT((caps & B(cap)) != 0, "GNP feature set");
    }
}

void testVerifiedDevicesStillFound()
{
    auto& registry = DeviceRegistry::instance();
    registry.initialize();

    // The products verified on hardware, with the collection each one exposes
    struct Verified {
        uint16_t pid;
        uint16_t usage;
    };
    for (auto [pid, usage] : { Verified { 0x24C7, 0x01 }, // Link 380
             Verified { 0x2E51, 0x05 }, Verified { 0x2E57, 0x05 }, // Link 390
             Verified { 0x2519, 0x01 }, // Evolve2 65 Flex
             Verified { 0x2548, 0x05 } }) { // Evolve3 75
        auto info = makeInfo(pid, 0xFF00, usage);
        JABRA_ASSERT(registry.findDevice(info) != nullptr, "verified Jabra product is found");
    }
}

void testListedProductsWinOverGenericDriver()
{
    auto& registry = DeviceRegistry::instance();
    registry.initialize();

    // Every device that lists product IDs still gets them; generic drivers only fill gaps
    for (const auto& device : registry.getAllDevices()) {
        for (uint16_t pid : device->getProductIds()) {
            auto info = makeInfo(pid, 0xFF00, 0x05, device->getVendorId());
            JABRA_ASSERT(registry.findDevice(info) == device.get(),
                std::string(device->getDeviceName()) + " keeps its product IDs");
        }
    }
}

void runAllJabraGnpTests()
{
    std::cout << "\n=== Jabra GNP Tests ===" << std::endl;
    testUsbHeadsetProbedAt08();
    testDongleFallsBackTo04();
    testDongleProbedFirstByName();
    testSilentAddressSkipped();
    testDongleWithoutHeadset();
    testStrayRepliesSkipped();
    testPaddedStringReply();
    testBatteryAbove100Rejected();
    testSidetoneLevels();
    testInactivityOfferedValues();
    testRegistryMatchesGnpCollection();
    testVerifiedDevicesStillFound();
    testListedProductsWinOverGenericDriver();
    std::cout << "  Jabra GNP tests passed" << std::endl;
}

} // namespace headsetcontrol::testing
