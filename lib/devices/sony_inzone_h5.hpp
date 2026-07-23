#pragma once

#include "../result_types.hpp"
#include "device_utils.hpp"
#include "hid_device.hpp"
#include <array>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief Sony INZONE H5 (WH-G500) wireless gaming headset
 *
 * Communicates via a 2.4 GHz USB dongle (VID 0x054C, PID 0x0EBF).
 *
 * The dongle exposes three HID top-level collections. The control protocol
 * lives on the Sony-vendor collection with usage page 0xFF04 (report ID 0x02,
 * 63-byte payload). It is a thin Sony vendor layer over standard Bluetooth
 * HCI: each report carries an HCI packet where the host issues commands
 * with opcode 0xFC00 and the dongle replies with vendor event code 0xFF.
 */
class SonyINZONEH5 : public HIDDevice {
public:
    static constexpr uint16_t VENDOR_SONY = 0x054C;
    static constexpr std::array<uint16_t, 1> PRODUCT_IDS { 0x0EBF };

    // HID transport
    static constexpr int REPORT_SIZE = 64;
    static constexpr uint8_t REPORT_ID = 0x02;

    // HCI shell constants
    static constexpr uint8_t HCI_TYPE_COMMAND = 0x01;
    static constexpr uint8_t HCI_TYPE_EVENT = 0x04;
    static constexpr uint8_t SONY_EVENT_CODE = 0xFF;
    static constexpr uint8_t SONY_OPCODE_LO = 0x00; // 0xFC00 LE
    static constexpr uint8_t SONY_OPCODE_HI = 0xFC;
    static constexpr uint8_t SONY_KEY_ID_LO = 0x96;
    static constexpr uint8_t SONY_KEY_ID_HI = 0xC3;

    // ADDRESS nibbles
    static constexpr uint8_t ADDR_PC = 0x1;
    static constexpr uint8_t ADDR_TX = 0x2;
    static constexpr uint8_t ADDR_RX = 0x4;
    static constexpr uint8_t ADDR_PC_TO_RX = (ADDR_RX << 4) | ADDR_PC; // 0x41
    static constexpr uint8_t ADDR_PC_TO_TX = (ADDR_TX << 4) | ADDR_PC; // 0x21

    // EVENT_TYPE values
    static constexpr uint8_t ETYPE_GET = 0x01;
    static constexpr uint8_t ETYPE_SET = 0x02;
    static constexpr uint8_t ETYPE_RET = 0x10;
    static constexpr uint8_t ETYPE_NTFY = 0x20;
    static constexpr uint8_t ETYPE_NTFY_ACTIVE = 0xA0;

    // EVENT_ID values used by this implementation
    static constexpr uint8_t EID_2GHZ_CONNECT_STATUS = 0x01;
    static constexpr uint8_t EID_BATTERY_INFO = 0x04;
    static constexpr uint8_t EID_HEADPHONE_VOLUME = 0x21;
    static constexpr uint8_t EID_GAME_CHAT_MIX_BALANCE = 0x22;
    static constexpr uint8_t EID_SIDETONE_VOLUME = 0x23;
    static constexpr uint8_t EID_MIC_VOLUME = 0x24;

    // Device-side ranges. Headphone volume is 0..50 and balance is 0..90 in
    // steps of 10. Sidetone and mic ranges are not yet verified — assumed to
    // follow the headphone convention (0..50); the setters clamp via map().
    static constexpr uint8_t DEVICE_VOLUME_MAX = 50;
    static constexpr uint8_t DEVICE_BALANCE_MAX = 90; // step 10
    static constexpr uint8_t DEVICE_SIDETONE_MAX = 50;
    static constexpr uint8_t DEVICE_MIC_VOL_MAX = 50;

    // Timeouts for matched-response wait
    static constexpr int READ_TIMEOUT_MS = 500;
    static constexpr int MAX_READ_ATTEMPTS = 10;

    constexpr uint16_t getVendorId() const override { return VENDOR_SONY; }

    std::vector<uint16_t> getProductIds() const override
    {
        return { PRODUCT_IDS.begin(), PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override { return "Sony INZONE H5"sv; }

    constexpr int getCapabilities() const override
    {
        return B(CAP_BATTERY_STATUS) | B(CAP_CHATMIX_STATUS)
            | B(CAP_SIDETONE) | B(CAP_MICROPHONE_VOLUME);
    }

    // The control protocol lives on the Sony-vendor collection (usage page
    // 0xFF04, usage 0x0002). The usagepage/usageid hints are used to pick
    // the right top-level collection on Windows; on Linux the dongle's HID
    // interface is selected by first match if interface_id is 0.
    constexpr capability_detail getCapabilityDetail([[maybe_unused]] enum capabilities cap) const override
    {
        return { .usagepage = 0xFF04, .usageid = 0x0002, .interface_id = 0 };
    }

    Result<BatteryResult> getBattery(hid_device* device_handle) override
    {
        auto resp = exchange(device_handle, ADDR_PC_TO_RX, EID_BATTERY_INFO, ETYPE_GET, {});
        if (!resp) {
            return resp.error();
        }
        const auto& payload = resp->payload;
        if (payload.size() < 2) {
            return DeviceError::protocolError("BATTERY_INFO payload too short");
        }

        uint8_t charger = payload[0];
        uint8_t percent = payload[1];

        // 0xFF placeholder = headset offline / no cached value
        if (percent == 0xFF) {
            return DeviceError::deviceOffline("Headset reports battery=0xFF (offline)");
        }
        if (percent > 100) {
            return DeviceError::protocolError(
                std::format("Invalid battery percent: {}", percent));
        }

        return BatteryResult {
            .level_percent = percent,
            .status = (charger != 0) ? BATTERY_CHARGING : BATTERY_AVAILABLE,
            .raw_data = payload,
        };
    }

    Result<ChatmixResult> getChatmix(hid_device* device_handle) override
    {
        auto resp = exchange(device_handle, ADDR_PC_TO_RX, EID_GAME_CHAT_MIX_BALANCE, ETYPE_GET, {});
        if (!resp) {
            return resp.error();
        }
        const auto& payload = resp->payload;
        if (payload.empty()) {
            return DeviceError::protocolError("GAME_CHAT_MIX_BALANCE payload empty");
        }

        // payload[0] = mixBalance: 0..90 in steps of 10, 0=full game, 90=full chat.
        uint8_t balance = payload[0];
        if (balance == 0xFF) {
            return DeviceError::deviceOffline("Headset offline");
        }
        if (balance > DEVICE_BALANCE_MAX) {
            return DeviceError::protocolError(
                std::format("Invalid balance: {}", balance));
        }

        int chat_pct = (balance * 100) / DEVICE_BALANCE_MAX;
        int game_pct = 100 - chat_pct;
        int level = map<int>(balance, 0, DEVICE_BALANCE_MAX, 0, 128);

        return ChatmixResult {
            .level = level,
            .game_volume_percent = game_pct,
            .chat_volume_percent = chat_pct,
        };
    }

    Result<SidetoneResult> setSidetone(hid_device* device_handle, uint8_t level) override
    {
        uint8_t dev_level = map<uint8_t>(level, 0, 128, 0, DEVICE_SIDETONE_MAX);
        // SIDETONE_VOLUME payload: [sidetoneVolValue, sidetoneVolPercent]
        // The percent byte is a UI label; the Hub sends 0xFF as placeholder.
        std::array<uint8_t, 2> payload { dev_level, 0xFF };

        auto resp = exchange(device_handle, ADDR_PC_TO_RX, EID_SIDETONE_VOLUME, ETYPE_SET,
            std::span<const uint8_t> { payload });
        if (!resp) {
            return resp.error();
        }

        return SidetoneResult {
            .current_level = level,
            .min_level = 0,
            .max_level = 128,
            .device_min = 0,
            .device_max = DEVICE_SIDETONE_MAX,
        };
    }

    Result<MicVolumeResult> setMicVolume(hid_device* device_handle, uint8_t volume) override
    {
        uint8_t dev_level = map<uint8_t>(volume, 0, 128, 0, DEVICE_MIC_VOL_MAX);
        // MIC_VOLUME payload: [micMute, micVolValue, micVolPercent]
        std::array<uint8_t, 3> payload { 0x00, dev_level, 0xFF };

        auto resp = exchange(device_handle, ADDR_PC_TO_RX, EID_MIC_VOLUME, ETYPE_SET,
            std::span<const uint8_t> { payload });
        if (!resp) {
            return resp.error();
        }

        return MicVolumeResult {
            .volume = volume,
            .min_volume = 0,
            .max_volume = 128,
        };
    }

private:
    struct ParsedEvent {
        uint8_t event_id = 0;
        uint8_t event_type = 0;
        uint8_t address = 0;
        uint16_t transaction_id = 0;
        std::vector<uint8_t> payload;
    };

    /**
     * @brief Send an HCI COMMAND and wait for the matching EVENT response.
     *
     * The dongle responds to a GET with EVENT_TYPE.RET and to a SET with
     * EVENT_TYPE.NTFY, both carrying back the same TID we sent. Unsolicited
     * NTFY_ACTIVE events may arrive on the same channel; we skip frames that
     * don't match our request.
     */
    Result<ParsedEvent> exchange(hid_device* device_handle, uint8_t address,
        uint8_t event_id, uint8_t event_type, std::span<const uint8_t> payload)
    {
        uint16_t tid = ++transaction_counter_;
        if (tid <= 1) {
            // Skip TID 0 (overflow) and TID 1: the dongle's own unsolicited
            // NTFY_ACTIVE pushes carry TID=1, so reusing it would let a push be
            // matched as our reply.
            tid = transaction_counter_ = 2;
        }

        std::array<uint8_t, REPORT_SIZE> buf {};
        buildCommand(buf, address, event_id, event_type, tid, payload);

        if (auto wr = writeHID(device_handle, buf); !wr) {
            return wr.error();
        }

        const uint8_t want_type = (event_type == ETYPE_SET) ? ETYPE_NTFY : ETYPE_RET;

        for (int attempt = 0; attempt < MAX_READ_ATTEMPTS; ++attempt) {
            std::array<uint8_t, REPORT_SIZE> resp {};
            auto rd = readHIDTimeout(device_handle, resp, READ_TIMEOUT_MS);
            if (!rd) {
                if (rd.error().code == DeviceError::Code::Timeout) {
                    continue;
                }
                return rd.error();
            }

            auto parsed = parseEvent(resp);
            if (!parsed) {
                continue; // not an HCI event, or failed validation
            }

            // Prefer a response that matches both event_id and our TID
            // (which also implicitly filters out unsolicited NTFY_ACTIVE,
            // since those carry TID=1 originated by the dongle).
            if (parsed->event_id == event_id
                && parsed->transaction_id == tid
                && (parsed->event_type == want_type
                    || parsed->event_type == ETYPE_NTFY_ACTIVE)) {
                return *parsed;
            }
            // Otherwise keep reading — it may be an unrelated NTFY_ACTIVE.
        }

        return DeviceError::timeout(
            std::format("No response for event_id 0x{:02x} (TID {})", event_id, tid));
    }

    /**
     * @brief Build a Sony vendor HCI COMMAND in the host write buffer.
     *
     * Layout (post-report-ID offsets):
     *   [0]      hid_length  = 12 + len(payload)
     *   [1]      hci_type    = 0x01 (COMMAND)
     *   [2..3]   opcode      = 0xFC00 (LE)
     *   [4]      param_length = 8 + len(payload)
     *   [5..6]   sony_key_id = 0xC396 (LE)
     *   [7]      address     = (Dst<<4) | Src
     *   [8]      event_id
     *   [9]      event_type
     *   [10..11] transaction_id (LE)
     *   [12..]   payload
     *   [..]     checksum    = sum(post-rid[4..end-1]) & 0xFF
     *
     * Map to buf indices (buf[0] = report ID): each post-rid offset N maps to
     * buf[N+1].
     */
    static void buildCommand(std::array<uint8_t, REPORT_SIZE>& buf,
        uint8_t address, uint8_t event_id, uint8_t event_type,
        uint16_t tid, std::span<const uint8_t> payload)
    {
        const size_t payload_len = payload.size();
        const size_t hid_length = 12 + payload_len; // HCI byte count

        buf[0] = REPORT_ID;
        buf[1] = static_cast<uint8_t>(hid_length);

        buf[2] = HCI_TYPE_COMMAND;
        buf[3] = SONY_OPCODE_LO;
        buf[4] = SONY_OPCODE_HI;
        buf[5] = static_cast<uint8_t>(8 + payload_len); // param_length
        buf[6] = SONY_KEY_ID_LO;
        buf[7] = SONY_KEY_ID_HI;
        buf[8] = address;
        buf[9] = event_id;
        buf[10] = event_type;
        buf[11] = static_cast<uint8_t>(tid & 0xFF);
        buf[12] = static_cast<uint8_t>((tid >> 8) & 0xFF);

        for (size_t i = 0; i < payload_len; ++i) {
            buf[13 + i] = payload[i];
        }

        // checksum = sum(buf[6..12+payload_len]) & 0xFF
        unsigned sum = 0;
        for (size_t i = 6; i <= 12 + payload_len; ++i) {
            sum += buf[i];
        }
        buf[13 + payload_len] = static_cast<uint8_t>(sum & 0xFF);
    }

    /**
     * @brief Validate and parse an incoming HCI EVENT report.
     *
     * Returns std::nullopt for any frame that isn't a well-formed Sony
     * vendor EVENT addressed to the host. The HCI checksum is verified.
     */
    static std::optional<ParsedEvent> parseEvent(const std::array<uint8_t, REPORT_SIZE>& buf)
    {
        if (buf[0] != REPORT_ID) {
            return std::nullopt;
        }
        const uint8_t hid_length = buf[1];
        if (hid_length < 12 || hid_length > REPORT_SIZE - 2) {
            return std::nullopt;
        }

        // HCI shell checks
        if (buf[2] != HCI_TYPE_EVENT)
            return std::nullopt;
        if (buf[3] != SONY_EVENT_CODE)
            return std::nullopt;
        // buf[4] = param_length (informational; ignore)
        if (buf[5] != 0x00) // dummy byte
            return std::nullopt;
        if (buf[6] != SONY_KEY_ID_LO || buf[7] != SONY_KEY_ID_HI)
            return std::nullopt;

        const uint8_t address = buf[8];
        // Destination must be PC (high nibble == 1)
        if ((address >> 4) != ADDR_PC)
            return std::nullopt;

        // Checksum: sum(HCI[3..N-1]) & 0xFF == HCI[N], where HCI[i] = buf[i+2].
        // i.e. sum(buf[5..hid_length]) & 0xFF == buf[hid_length+1].
        unsigned sum = 0;
        for (size_t i = 5; i <= hid_length; ++i) {
            sum += buf[i];
        }
        if (static_cast<uint8_t>(sum & 0xFF) != buf[hid_length + 1]) {
            return std::nullopt;
        }

        // Payload runs from buf[13] up to buf[hid_length] inclusive.
        return ParsedEvent {
            .event_id = buf[9],
            .event_type = buf[10],
            .address = address,
            .transaction_id = static_cast<uint16_t>(buf[11] | (buf[12] << 8)),
            .payload = (hid_length > 12)
                ? std::vector<uint8_t>(buf.begin() + 13, buf.begin() + hid_length + 1)
                : std::vector<uint8_t> {},
        };
    }

    uint16_t transaction_counter_ = 0;
};

} // namespace headsetcontrol
