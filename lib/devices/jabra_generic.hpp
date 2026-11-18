#pragma once

#include "protocols/jabra_gnp_protocol.hpp"
#include <algorithm>
#include <array>
#include <cwchar>
#include <map>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Every Jabra headset and Link dongle that speaks the GN Protocol (GNP)
 *
 * Jabra uses the same GNP command set across its current headsets and Link dongles, so
 * instead of listing product IDs this driver takes every Jabra (0x0B0E) HID interface
 * that is the GNP vendor collection (usage page 0xFF00, usage 0x05 or 0x01). What differs
 * between products is worked out per device:
 *
 *   - the collection to open: 0x05 if the product has it, else 0x01
 *   - the headset's GNP address: 0x08 for a headset on its own USB cable, 0x04 for the
 *     headset behind a Link dongle. Both are probed with an IDENT name request (Link
 *     dongles, by their USB product string, are tried at 0x04 first) and the answer
 *     is kept for the open handle.
 *
 * DECT bases are left out (isDectBase()). The real model name comes from the headset
 * (getMetadata()). Settings a model lacks
 * are rejected by the headset with a NACK and reported as not supported.
 *
 * Verified on hardware:
 *   - Link 380 (0x24C7, fw 1.16) with an Evolve2 65: collection 0x01, Linux
 *   - Link 390 (0x2E51, 0x2E57; fw 1.3.0) with an Evolve2 65 Flex (fw 1.2.14), Linux and
 *     macOS, and with an Evolve3 75 on Windows: collection 0x05. Through it, the
 *     inactivity timer (never, 30 min) was checked against what Jabra Direct shows.
 *   - Evolve2 65 Flex on USB (0x2519, fw 1.2.14): collection 0x01, Linux and macOS
 *   - Evolve3 75 on USB (0x2548): collection 0x05, Windows; battery, sidetone, busylight,
 *     voice prompts, inactivity and IntelliTone
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
        return "Jabra (GN Protocol)"sv;
    }

    bool matchesUnlistedInterface(const hid_device_info& info) const override
    {
        return info.vendor_id == VENDOR_JABRA && isGnpCollection(info) && !isDectBase(info.product_id);
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

    static constexpr bool isDectBase(uint16_t product_id)
    {
        return std::binary_search(DECT_BASE_PIDS.begin(), DECT_BASE_PIDS.end(), product_id);
    }

    void onConnectionClosed(hid_device* device_handle) const override
    {
        addresses_.erase(device_handle);
    }

protected:
    Result<uint8_t> headsetAddress(hid_device* device_handle) override
    {
        if (auto it = addresses_.find(device_handle); it != addresses_.end()) {
            return it->second;
        }

        const auto order = looksLikeDongle(device_handle)
            ? std::array<uint8_t, 2> { ADDR_DONGLE_HS, ADDR_USB_HS }
            : std::array<uint8_t, 2> { ADDR_USB_HS, ADDR_DONGLE_HS };

        // A wrong first guess may not answer at all, so it gets a short timeout
        const int first_timeout = std::min(hsc_device_timeout, PROBE_TIMEOUT_MS);

        bool dongle_without_headset = false;
        for (uint8_t address : order) {
            const int timeout = address == order.front() ? first_timeout : hsc_device_timeout;
            auto reply        = exchange(device_handle, address, TYPE_GET, CMD_IDENT, IDENT_NAME, {}, timeout);
            if (!reply) {
                if (reply.error().code == DeviceError::Code::Timeout) {
                    continue; // nothing answers there, try the other address
                }
                return reply.error();
            }
            if ((*reply)[OFF_CMD] != CMD_NACK) {
                addresses_[device_handle] = address;
                return address;
            }
            // A dongle reports its paired headset offline rather than absent; keep probing
            // in case this is a USB headset, but remember why 0x04 failed.
            if (address == ADDR_DONGLE_HS && (*reply)[OFF_SUB] == NACK_PEER_OFFLINE) {
                dongle_without_headset = true;
            }
        }

        if (dongle_without_headset) {
            return DeviceError::deviceOffline("Headset not connected to the Jabra dongle (powered off or out of range)");
        }
        return DeviceError::deviceOffline("No Jabra headset answered at GNP address 0x08 or 0x04");
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

private:
    static constexpr int PROBE_TIMEOUT_MS = 1000;

    // Probed headset address per open handle; the registry shares one driver instance
    // between all connected Jabra devices.
    mutable std::map<hid_device*, uint8_t> addresses_;
};

} // namespace headsetcontrol
