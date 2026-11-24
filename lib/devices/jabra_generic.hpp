#pragma once

#include "protocols/jabra_gnp_protocol.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cwchar>
#include <map>
#include <mutex>
#include <optional>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Fallback for Jabra products that speak the GN Protocol (GNP) but no driver lists
 *
 * The named drivers (Link 380, Link 390, Evolve2 65 Flex, Evolve3 75) keep their product
 * IDs; DeviceRegistry::findDevice() only offers this driver what none of them lists. It
 * takes a Jabra (0x0B0E) HID interface that is the GNP vendor collection (usage page
 * 0xFF00, usage 0x05 or 0x01), and works out per device what the named drivers fix:
 *
 *   - the collection to open: 0x05 if the product has it, else 0x01
 *   - the headset's GNP address: 0x08 for a headset on its own USB cable, 0x04 for the
 *     headset behind a Link dongle. Both are probed with an IDENT name request (Link
 *     dongles, by their USB product string, are tried at 0x04 first) and the answer
 *     is kept for the open handle. An address only a NACK points to (a dongle that
 *     reports its headset off, or a device that does not know the name request) and a
 *     probe that met silence stand for a few seconds, so one command line does not probe
 *     once per setting, and a long-lived handle still probes again later.
 *
 * The models it ends up serving are untested, hence its name. The real model name comes
 * from the headset (getMetadata()); settings a model lacks are rejected by the headset
 * with a NACK and reported as not supported.
 *
 * Left out (isLeftOut()): DECT bases, which answer at address 8 as the base, and products
 * that are not known to work like the tested ones (see LEFT_OUT_PIDS).
 *
 * Jabra Direct's FWU/DeviceInfo.xml lists the GNP address of every product (8 = headset
 * on USB, 1 = Link dongle, whose headset answers at 4), which is what the probe expects.
 */
class JabraGeneric : public protocols::JabraGNPBase {
public:
    constexpr std::vector<uint16_t> getProductIds() const override
    {
        return {};
    }

    constexpr std::string_view getDeviceName() const override
    {
        return "Jabra (generic, untested model)"sv;
    }

    bool isFallbackDriver() const override
    {
        return true;
    }

    bool matchesUnlistedInterface(const hid_device_info& info) const override
    {
        return info.vendor_id == VENDOR_JABRA && isGnpCollection(info) && !isLeftOut(info.product_id);
    }

    /**
     * @brief DECT bases and dongles (Engage, PRO 9xx, GO 6400, Link 400 and rebrands).
     *
     * Their base answers at GNP address 8 as well as 1, with the headset at 10, so the
     * probe would take the base for the headset and send it headset settings. From Jabra
     * Direct's DeviceInfo.xml.
     */
    static constexpr std::array<uint16_t, 58> DECT_BASE_PIDS {
        0x1000, 0x1016, 0x1017, 0x1018, 0x1021, 0x1022, 0x1023, 0x1037, 0x1038, 0x1039, 0x1041,
        0x1042, 0x1043, 0x1110, 0x1112, 0x1113, 0x1118, 0x111A, 0x111B, 0x111D, 0x111E, 0x111F,
        0x1120, 0x1121, 0x1122, 0x1123, 0x1124, 0x1126, 0x1131, 0x1132, 0x1133, 0x1134, 0x1135,
        0x1136, 0x1140, 0x1141, 0x1142, 0x1146, 0x1147, 0x1148, 0x114A, 0x114B, 0x114C, 0x114D,
        0x114E, 0x114F, 0x1150, 0x1151, 0xA700, 0xA701, 0xA702, 0xA703, 0xA704, 0xA705, 0xA706,
        0xAE60, 0xAE62, 0xAE63
    };

    /**
     * @brief Relatives of the named products that were left out of them on purpose.
     *
     * Link 380a Spk MS (0x24E9), a speakerphone variant, and the Evolve2 65 Flex Secure
     * (0x2523-0x2525), which runs separate firmware.
     */
    static constexpr std::array<uint16_t, 4> LEFT_OUT_PIDS { 0x24E9, 0x2523, 0x2524, 0x2525 };

    static constexpr bool isLeftOut(uint16_t product_id)
    {
        return std::binary_search(DECT_BASE_PIDS.begin(), DECT_BASE_PIDS.end(), product_id)
            || std::find(LEFT_OUT_PIDS.begin(), LEFT_OUT_PIDS.end(), product_id) != LEFT_OUT_PIDS.end();
    }

    constexpr capability_detail getCapabilityDetail([[maybe_unused]] enum capabilities cap) const override
    {
        return { .usagepage = GNP_USAGE_PAGE, .usageid = GNP_USAGES.front(), .interface_id = 0x03 };
    }

    /**
     * @brief The GNP collection of a connected product, looked up again after a few seconds.
     *
     * Products differ (0x05 on the Link 390, 0x01 on the Evolve2 65 Flex); this matters
     * on Windows, where every collection is its own HID path. The answer is kept briefly
     * so a burst of requests enumerates once, but not for the life of the process: a
     * device that is replugged or updated is looked at again. Falls back to
     * getCapabilityDetail() while the product is not connected or its collections are
     * not reported.
     */
    capability_detail resolveCapabilityDetail(uint16_t product_id, enum capabilities cap) const override
    {
        const auto now = std::chrono::steady_clock::now();
        {
            std::lock_guard lock(mutex_);
            if (auto it = collections_.find(product_id); it != collections_.end() && now < it->second.until) {
                return it->second.detail;
            }
        }

        // Enumerating can take a while; other handles' requests need not wait for it
        hid_device_info* devs        = hid_enumerate(VENDOR_JABRA, product_id);
        const hid_device_info* found = pickGnpCollection(devs);
        capability_detail detail     = getCapabilityDetail(cap);
        if (found) {
            detail = { .usagepage = GNP_USAGE_PAGE, .usageid = found->usage, .interface_id = found->interface_number };
        }
        hid_free_enumeration(devs);

        std::lock_guard lock(mutex_);
        if (found) {
            collections_[product_id] = { .detail = detail, .until = now + std::chrono::milliseconds(COLLECTION_CACHE_MS) };
        } else {
            collections_.erase(product_id);
        }
        return detail;
    }

    /**
     * @brief The GNP collection to open among a product's enumerated interfaces.
     *
     * The preferred usage (0x05) wins wherever it is in the list; otherwise the first
     * GNP collection found (0x01). nullptr if the product reports none.
     */
    static constexpr const hid_device_info* pickGnpCollection(const hid_device_info* devs)
    {
        const hid_device_info* found = nullptr;
        for (const auto* cur = devs; cur; cur = cur->next) {
            if (!isGnpCollection(*cur)) {
                continue;
            }
            if (cur->usage == GNP_USAGES.front()) {
                return cur;
            }
            if (!found) {
                found = cur;
            }
        }
        return found;
    }

    void onConnectionClosed(hid_device* device_handle) const override
    {
        std::lock_guard lock(mutex_);
        addresses_.erase(device_handle);
        silent_until_.erase(device_handle);
    }

protected:
    Result<uint8_t> headsetAddress(hid_device* device_handle) override
    {
        {
            std::lock_guard lock(mutex_);
            const auto now = std::chrono::steady_clock::now();
            if (auto it = addresses_.find(device_handle); it != addresses_.end()) {
                if (!it->second.until || now < *it->second.until) {
                    return it->second.address;
                }
                addresses_.erase(it); // only a NACK pointed there, and that was a while ago
            }
            // A probe that met silence is slow, so it is not repeated for every request
            if (auto it = silent_until_.find(device_handle); it != silent_until_.end() && now < it->second) {
                return noHeadsetAnswered();
            }
        }

        const auto order = looksLikeDongle(device_handle)
            ? std::array<uint8_t, 2> { ADDR_DONGLE_HS, ADDR_USB_HS }
            : std::array<uint8_t, 2> { ADDR_USB_HS, ADDR_DONGLE_HS };

        // 0x08 is the USB headset, which answers quickly whenever it is there, so it gets a
        // short timeout wherever it is in the order. 0x04 goes over Bluetooth to the headset
        // behind the dongle and always gets the full timeout. A probe asks an address that
        // may not exist, so it never blocks: a negative (blocking) device timeout is replaced.
        const int full_timeout      = hsc_device_timeout < 0 ? PROBE_BLOCKING_TIMEOUT_MS : hsc_device_timeout;
        const int usb_probe_timeout = std::min(full_timeout, PROBE_TIMEOUT_MS);

        bool silence = false;
        // An address that NACKed the name request as unknown (F6, FF): something may be
        // there, but a dongle NACKs from the address asked too, so it is only used when the
        // other address turns out empty
        std::optional<uint8_t> unknown_request_at;
        for (uint8_t address : order) {
            const int timeout = address == ADDR_USB_HS ? usb_probe_timeout : full_timeout;
            auto reply        = exchange(device_handle, address, TYPE_GET, CMD_IDENT, IDENT_NAME, {}, timeout);
            if (!reply) {
                if (reply.error().code == DeviceError::Code::Timeout) {
                    silence = true;
                    continue; // nothing answers there, try the other address
                }
                return reply.error();
            }
            if ((*reply)[OFF_CMD] != CMD_NACK) {
                rememberAddress(device_handle, address);
                return address;
            }
            const uint8_t reason = (*reply)[OFF_SUB];
            if ((*reply)[OFF_SRC] == address && (reason == NACK_NO_GROUP || reason == NACK_NO_SUBCMD)) {
                if (!unknown_request_at) {
                    unknown_request_at = address;
                }
                continue;
            }
            // A dongle reports its paired headset as not connected rather than absent: this
            // is a dongle with its headset at 0x04, so 0x04 is remembered for a few seconds
            // and 0x08 is not asked again meanwhile. Until the headset comes on, each request
            // then costs one exchange that ends in the same NACK. A Link 390 with its Evolve3
            // 75 switched off answers F4 (no child) within ~150 ms; F3 (peer offline) is the
            // same case.
            if (address == ADDR_DONGLE_HS && (reason == NACK_NO_CHILD || reason == NACK_PEER_OFFLINE)) {
                rememberAddress(device_handle, address, Certainty::FromNack);
                return DeviceError::deviceOffline("Headset not connected to the Jabra dongle (powered off or out of range)");
            }
        }

        if (unknown_request_at) {
            rememberAddress(device_handle, *unknown_request_at, Certainty::FromNack);
            return *unknown_request_at;
        }
        if (silence) {
            std::lock_guard lock(mutex_);
            silent_until_[device_handle] = std::chrono::steady_clock::now() + std::chrono::milliseconds(PROBE_RETRY_MS);
        }
        return noHeadsetAnswered();
    }

    /**
     * @brief Link dongles are probed at 0x04 first; decided by the USB product string.
     */
    virtual bool looksLikeDongle(hid_device* device_handle) const
    {
        wchar_t product[128] = {};
        if (hid_get_product_string(device_handle, product, 128) != 0) {
            return false;
        }
        return std::wcsstr(product, L"Link") != nullptr;
    }

    /// How long an address that only a NACK pointed to is used before both are probed again
    std::chrono::milliseconds nack_address_lifetime_ { PROBE_RETRY_MS };

private:
    static constexpr int PROBE_TIMEOUT_MS          = 1000;
    static constexpr int PROBE_BLOCKING_TIMEOUT_MS = 5000;
    // How long a probe that met silence stands before the addresses are asked again
    static constexpr int PROBE_RETRY_MS = 5000;
    // How long a product's chosen collection is used before it is enumerated again
    static constexpr int COLLECTION_CACHE_MS = 5000;

    struct CachedCollection {
        capability_detail detail;
        std::chrono::steady_clock::time_point until;
    };

    /// A real answer settles the address for the handle; a NACK only points to it for a while
    enum class Certainty { Answered,
        FromNack };

    struct KnownAddress {
        uint8_t address;
        std::optional<std::chrono::steady_clock::time_point> until; // unset: for the handle's life
    };

    void rememberAddress(hid_device* device_handle, uint8_t address, Certainty certainty = Certainty::Answered) const
    {
        std::lock_guard lock(mutex_);
        silent_until_.erase(device_handle);
        // Another probe on the same handle may have got a real answer meanwhile; a NACK
        // does not replace that
        if (auto it = addresses_.find(device_handle);
            certainty == Certainty::FromNack && it != addresses_.end() && !it->second.until) {
            return;
        }
        KnownAddress known { .address = address, .until = std::nullopt };
        if (certainty == Certainty::FromNack) {
            known.until = std::chrono::steady_clock::now() + nack_address_lifetime_;
        }
        addresses_[device_handle] = known;
    }

    static DeviceError noHeadsetAnswered()
    {
        return DeviceError::deviceOffline("No Jabra headset answered at GNP address 0x08 or 0x04");
    }

    // GNP collection found per product ID, so a burst of requests enumerates once
    mutable std::map<uint16_t, CachedCollection> collections_;

    // Probed headset address per open handle; the registry shares one driver instance
    // between all connected Jabra devices.
    mutable std::map<hid_device*, KnownAddress> addresses_;

    // Handles whose last probe met silence, and until when that result stands
    mutable std::map<hid_device*, std::chrono::steady_clock::time_point> silent_until_;

    // Guards the maps above: Headsets on different threads share this driver
    mutable std::mutex mutex_;
};

} // namespace headsetcontrol
