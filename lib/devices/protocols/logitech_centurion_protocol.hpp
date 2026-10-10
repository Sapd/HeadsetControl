#pragma once

#include "../../device.hpp"
#include "../../result_types.hpp"
#include "../hid_device.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace headsetcontrol::protocols {

enum class CenturionFeature : uint16_t {
    Root                  = 0x0000,
    FeatureSet            = 0x0001,
    CenturionBridge       = 0x0003,
    CenturionBatterySoc   = 0x0104,
    CenturionAutoSleep    = 0x0108,
    HeadsetAdvancedParaEQ = 0x020d,
    HeadsetMicMuteLed     = 0x0601,
    HeadsetOnboardEQ      = 0x0636,
    HeadsetAudioSidetone  = 0x0604,
};

struct CenturionFeatureInfo {
    uint8_t index   = 0;
    uint8_t version = 0;
    uint8_t flags   = 0;
};

/**
 * @brief Opt-in protocol behaviors, all off by default.
 *
 * Devices that have not been verified with a behavior keep the defaults, so
 * their traffic stays exactly as before the behavior was introduced.
 */
struct CenturionOptions {
    // Set the length byte of direct (non-bridged) requests to the real payload
    // size instead of the full 64-byte payload buffer.
    bool exact_direct_length = false;
    // Report a bridge "headset disconnected" event as DeviceError::deviceOffline.
    bool detect_connection_events = false;
    // After a bridge ACK followed by a read timeout, query the bridge's
    // connection status once and report deviceOffline if the headset is gone.
    bool probe_offline_on_timeout = false;
    // Look up each feature index with Root.getFeature when first needed,
    // instead of enumerating every feature of the dongle and the headset.
    bool lookup_features_by_id = false;
};

class LogitechCenturionProtocol : public HIDDevice {
protected:
    static constexpr uint8_t REPORT_ID = 0x51;
    static constexpr std::array<uint8_t, 1> DEFAULT_FRAME_PREFIX { REPORT_ID };
    static constexpr size_t FRAME_SIZE                  = 64;
    static constexpr uint8_t SOFTWARE_ID                = 0x01;
    static constexpr uint8_t BRIDGE_SEND_FRAGMENT_FN    = 0x10;
    static constexpr uint8_t BRIDGE_MESSAGE_EVENT_FN    = 0x10;
    static constexpr int POLL_ATTEMPTS                  = 8;
    static constexpr size_t MAX_SINGLE_BRIDGE_PAYLOAD   = 56;
    static constexpr size_t MAX_CONTINUATION_PAYLOAD    = 60;
    static constexpr size_t MAX_BRIDGE_SUB_MESSAGE_SIZE = 0x0FFF;

    /**
     * @brief Bytes that start every frame, before the length byte.
     *
     * The G PRO X 2 uses report ID 0x51; other Centurion devices use a longer
     * prefix (the G522 uses 0x50 0x23).
     */
    virtual std::span<const uint8_t> centurionFramePrefix() const
    {
        return DEFAULT_FRAME_PREFIX;
    }

    virtual CenturionOptions centurionOptions() const
    {
        return {};
    }

    /**
     * @param match_function Only accept a reply that echoes the request's
     *        function and software ID, not just its feature index.
     */
    [[nodiscard]] Result<std::vector<uint8_t>> sendCenturionRequest(
        hid_device* device_handle,
        uint8_t feature_index,
        uint8_t function,
        std::span<const uint8_t> params = {},
        bool match_function             = false) const
    {
        const auto options = centurionOptions();
        const auto prefix  = centurionFramePrefix();

        std::array<uint8_t, FRAME_SIZE> frame {};
        if (options.exact_direct_length) {
            frame = buildCenturionFrame(buildDirectPayloadVector(feature_index, function, params), 0x00, prefix);
        } else {
            frame = buildCenturionFrame(buildDirectPayload(feature_index, function, params), 0x00, prefix);
        }

        if (auto write_result = this->writeHID(device_handle, frame, frame.size()); !write_result) {
            return write_result.error();
        }

        const auto function_sw = static_cast<uint8_t>((function & 0xF0) | SOFTWARE_ID);

        for (int attempt = 0; attempt < POLL_ATTEMPTS; ++attempt) {
            std::array<uint8_t, FRAME_SIZE> response {};
            auto read_result = this->readHIDTimeout(device_handle, response, hsc_device_timeout);
            if (!read_result) {
                return read_result.error();
            }

            auto payload_result = extractCenturionPayload(response, prefix);
            if (!payload_result) {
                return payload_result.error();
            }

            const auto& reply = *payload_result;
            if (reply.size() < 2) {
                continue;
            }

            if (options.detect_connection_events && centurion_bridge_index_.has_value()
                && isBridgeConnectionEvent(reply, *centurion_bridge_index_)) {
                if (isBridgeDisconnectedEvent(reply, *centurion_bridge_index_)) {
                    return DeviceError::deviceOffline("Headset is powered off or not connected");
                }
                continue;
            }

            if (reply[0] != feature_index) {
                continue;
            }

            if (match_function && reply[1] != function_sw) {
                continue;
            }

            return std::vector<uint8_t>(reply.begin() + 2, reply.end());
        }

        return DeviceError::timeout("Timed out waiting for Centurion feature response");
    }

    [[nodiscard]] Result<CenturionFeatureInfo> getCenturionFeatureInfo(
        hid_device* device_handle,
        uint16_t feature_id) const
    {
        if (centurionOptions().lookup_features_by_id) {
            return lookupCenturionFeature(device_handle, feature_id);
        }

        auto init_result = ensureCenturionFeaturesDiscovered(device_handle);
        if (!init_result) {
            return init_result.error();
        }

        auto it = centurion_sub_features_.find(feature_id);
        if (it == centurion_sub_features_.end()) {
            return DeviceError::notSupported("Centurion feature not available on this device");
        }

        return it->second;
    }

    [[nodiscard]] Result<std::vector<uint8_t>> sendCenturionFeatureRequest(
        hid_device* device_handle,
        uint16_t feature_id,
        uint8_t function,
        std::span<const uint8_t> params = {}) const
    {
        auto feature_info = getCenturionFeatureInfo(device_handle, feature_id);
        if (!feature_info) {
            return feature_info.error();
        }

        return sendCenturionBridgeRequest(device_handle, feature_info->index, function, params);
    }

public:
    static constexpr bool isBridgeSubMessageSizeSupported(size_t sub_message_size)
    {
        return sub_message_size <= MAX_BRIDGE_SUB_MESSAGE_SIZE;
    }

    static constexpr auto buildCenturionFrame(
        std::span<const uint8_t> payload,
        uint8_t flags                   = 0x00,
        std::span<const uint8_t> prefix = DEFAULT_FRAME_PREFIX) -> std::array<uint8_t, FRAME_SIZE>
    {
        std::array<uint8_t, FRAME_SIZE> frame {};
        const size_t header = prefix.size();
        for (size_t i = 0; i < header; ++i) {
            frame[i] = prefix[i];
        }
        frame[header]     = static_cast<uint8_t>(payload.size() + 1);
        frame[header + 1] = flags;

        for (size_t i = 0; i < payload.size() && (i + header + 2) < frame.size(); ++i) {
            frame[i + header + 2] = payload[i];
        }

        return frame;
    }

    /**
     * @brief Return the payload of a frame (the bytes after the length and flags bytes).
     */
    static Result<std::vector<uint8_t>> extractCenturionPayload(
        std::span<const uint8_t> frame,
        std::span<const uint8_t> prefix = DEFAULT_FRAME_PREFIX)
    {
        const size_t header = prefix.size();
        if (frame.size() < header + 3 || !std::equal(prefix.begin(), prefix.end(), frame.begin())) {
            return DeviceError::protocolError("Unexpected Centurion frame prefix");
        }

        size_t cpl_length = frame[header];
        if (cpl_length <= 1 || (cpl_length + header + 1) > frame.size()) {
            return DeviceError::protocolError("Invalid Centurion frame length");
        }

        return std::vector<uint8_t>(frame.begin() + static_cast<std::ptrdiff_t>(header + 2),
            frame.begin() + static_cast<std::ptrdiff_t>(header + 1 + cpl_length));
    }

    /**
     * @brief Check for the bridge's connection event: function 0, software ID 0.
     *
     * Payload layout: [bridge index, 0x00, 0x00, connected]. The dongle sends it
     * when the headset behind the bridge powers on (connected = 1) or off (0).
     */
    static constexpr bool isBridgeConnectionEvent(std::span<const uint8_t> reply, uint8_t bridge_index)
    {
        return reply.size() >= 4 && reply[0] == bridge_index && reply[1] == 0x00;
    }

    static constexpr bool isBridgeDisconnectedEvent(std::span<const uint8_t> reply, uint8_t bridge_index)
    {
        return isBridgeConnectionEvent(reply, bridge_index) && reply[3] == 0x00;
    }

    /**
     * @brief Check the parameters of a bridge status reply (bridge function 0).
     *
     * Parameters 2-3 count the connected headsets: 03 e8 00 01 01 while the
     * headset is on, 03 e8 00 00 01 while it is off.
     */
    static constexpr bool isBridgeStatusDisconnected(std::span<const uint8_t> params)
    {
        return params.size() >= 4 && params[2] == 0x00 && params[3] == 0x00;
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
        auto status = (charging_state == 1 || charging_state == 2)
            ? BATTERY_CHARGING
            : BATTERY_AVAILABLE;

        return BatteryResult {
            .level_percent = level,
            .status        = status,
        };
    }

    static constexpr auto buildBridgeSubMessage(
        uint8_t sub_feature_index,
        uint8_t function,
        std::span<const uint8_t> params,
        uint8_t software_id = SOFTWARE_ID) -> std::array<uint8_t, 64>
    {
        std::array<uint8_t, 64> message {};
        message[0] = 0x00;
        message[1] = sub_feature_index;
        message[2] = static_cast<uint8_t>((function & 0xF0) | (software_id & 0x0F));

        for (size_t i = 0; i < params.size() && (i + 3) < message.size(); ++i) {
            message[i + 3] = params[i];
        }

        return message;
    }

    static auto buildBridgeSubMessageVector(
        uint8_t sub_feature_index,
        uint8_t function,
        std::span<const uint8_t> params,
        uint8_t software_id = SOFTWARE_ID) -> std::vector<uint8_t>
    {
        std::vector<uint8_t> message;
        message.reserve(params.size() + 3);
        message.push_back(0x00);
        message.push_back(sub_feature_index);
        message.push_back(static_cast<uint8_t>((function & 0xF0) | (software_id & 0x0F)));
        message.insert(message.end(), params.begin(), params.end());
        return message;
    }

    static constexpr bool isBridgeResponseFor(std::span<const uint8_t> reply_data, uint8_t expected_sub_feature_index)
    {
        if (reply_data.size() < 6) {
            return false;
        }

        uint8_t sub_cpl     = reply_data[4];
        uint8_t sub_feature = reply_data[5];
        if (sub_cpl != 0x00) {
            return false;
        }

        if (sub_feature == expected_sub_feature_index) {
            return true;
        }

        return sub_feature == 0xFF && reply_data.size() >= 7 && reply_data[6] == expected_sub_feature_index;
    }

    static Result<std::vector<uint8_t>> parseBridgeResponse(std::span<const uint8_t> reply_data)
    {
        if (reply_data.size() < 7) {
            return DeviceError::protocolError("Malformed Centurion bridge response");
        }

        if (reply_data[5] == 0xFF) {
            return DeviceError::protocolError("Centurion sub-device rejected the request");
        }

        return std::vector<uint8_t>(reply_data.begin() + 7, reply_data.end());
    }

private:
    static constexpr std::array<uint8_t, 2> featureIdParams(uint16_t feature_id)
    {
        return { static_cast<uint8_t>(feature_id >> 8), static_cast<uint8_t>(feature_id & 0xFF) };
    }

    /**
     * @brief Find one sub-device feature with Root.getFeature, through the bridge.
     *
     * Takes one request per feature (plus one for the bridge itself) instead of
     * enumerating every feature. Root.getFeature replies [index, type, version];
     * index 0 means the feature does not exist.
     */
    [[nodiscard]] Result<CenturionFeatureInfo> lookupCenturionFeature(
        hid_device* device_handle,
        uint16_t feature_id) const
    {
        if (auto it = centurion_sub_features_.find(feature_id); it != centurion_sub_features_.end()) {
            return it->second;
        }

        if (!centurion_bridge_index_.has_value()) {
            auto bridge_lookup = sendCenturionRequest(
                device_handle,
                static_cast<uint8_t>(CenturionFeature::Root),
                0x00,
                featureIdParams(static_cast<uint16_t>(CenturionFeature::CenturionBridge)));
            if (!bridge_lookup) {
                return bridge_lookup.error();
            }
            if (bridge_lookup->empty() || (*bridge_lookup)[0] == 0) {
                return DeviceError::protocolError("Centurion bridge feature not found");
            }
            centurion_bridge_index_ = (*bridge_lookup)[0];
        }

        auto feature_lookup = sendCenturionBridgeRequest(
            device_handle,
            static_cast<uint8_t>(CenturionFeature::Root),
            0x00,
            featureIdParams(feature_id));
        if (!feature_lookup) {
            return feature_lookup.error();
        }
        if (feature_lookup->empty()
            || ((*feature_lookup)[0] == 0 && feature_id != static_cast<uint16_t>(CenturionFeature::Root))) {
            return DeviceError::notSupported("Centurion feature not available on this device");
        }

        CenturionFeatureInfo info {
            .index   = (*feature_lookup)[0],
            .version = static_cast<uint8_t>(feature_lookup->size() > 2 ? (*feature_lookup)[2] : 0),
            .flags   = static_cast<uint8_t>(feature_lookup->size() > 1 ? (*feature_lookup)[1] : 0),
        };
        centurion_sub_features_[feature_id] = info;
        return info;
    }

    [[nodiscard]] Result<void> ensureCenturionFeaturesDiscovered(hid_device* device_handle) const
    {
        if (centurion_features_discovered_) {
            return {};
        }

        auto feature_set_lookup = sendCenturionRequest(
            device_handle,
            static_cast<uint8_t>(CenturionFeature::Root),
            0x00,
            std::array<uint8_t, 2> {
                static_cast<uint8_t>(static_cast<uint16_t>(CenturionFeature::FeatureSet) >> 8),
                static_cast<uint8_t>(static_cast<uint16_t>(CenturionFeature::FeatureSet) & 0xFF) });
        if (!feature_set_lookup) {
            return feature_set_lookup.error();
        }
        if (feature_set_lookup->empty() || (*feature_set_lookup)[0] == 0) {
            return DeviceError::protocolError("Centurion FeatureSet not found");
        }

        uint8_t feature_set_index = (*feature_set_lookup)[0];

        auto feature_count_reply = sendCenturionRequest(device_handle, feature_set_index, 0x00);
        if (!feature_count_reply) {
            return feature_count_reply.error();
        }
        if (feature_count_reply->empty()) {
            return DeviceError::protocolError("Centurion FeatureSet count response was empty");
        }

        uint8_t feature_count = (*feature_count_reply)[0];
        uint8_t bridge_index  = 0xFF;
        for (uint8_t index = 0; index < feature_count; ++index) {
            auto feature_reply = sendCenturionRequest(device_handle, feature_set_index, 0x10, std::array<uint8_t, 1> { index });
            if (!feature_reply) {
                return feature_reply.error();
            }
            if (feature_reply->size() < 3) {
                continue;
            }

            uint16_t feature_id = (static_cast<uint16_t>((*feature_reply)[1]) << 8) | (*feature_reply)[2];
            if (feature_id == static_cast<uint16_t>(CenturionFeature::CenturionBridge)) {
                bridge_index = index;
                break;
            }
        }

        if (bridge_index == 0xFF) {
            return DeviceError::protocolError("Centurion bridge feature not found");
        }

        centurion_bridge_index_ = bridge_index;

        auto sub_feature_set_lookup = sendCenturionBridgeRequest(
            device_handle,
            static_cast<uint8_t>(CenturionFeature::Root),
            0x00,
            std::array<uint8_t, 2> {
                static_cast<uint8_t>(static_cast<uint16_t>(CenturionFeature::FeatureSet) >> 8),
                static_cast<uint8_t>(static_cast<uint16_t>(CenturionFeature::FeatureSet) & 0xFF) });
        if (!sub_feature_set_lookup) {
            return sub_feature_set_lookup.error();
        }
        if (sub_feature_set_lookup->empty() || (*sub_feature_set_lookup)[0] == 0) {
            return DeviceError::protocolError("Centurion sub-device FeatureSet not found");
        }

        uint8_t sub_feature_set_index = (*sub_feature_set_lookup)[0];

        auto sub_feature_count_reply = sendCenturionBridgeRequest(device_handle, sub_feature_set_index, 0x00);
        if (!sub_feature_count_reply) {
            return sub_feature_count_reply.error();
        }
        if (sub_feature_count_reply->empty()) {
            return DeviceError::protocolError("Centurion sub-device FeatureSet count response was empty");
        }

        uint8_t sub_feature_count = (*sub_feature_count_reply)[0];
        centurion_sub_features_.clear();

        for (uint8_t index = 0; index < sub_feature_count; ++index) {
            auto feature_reply = sendCenturionBridgeRequest(device_handle, sub_feature_set_index, 0x10, std::array<uint8_t, 1> { index });
            if (!feature_reply) {
                return feature_reply.error();
            }
            if (feature_reply->size() < 3) {
                continue;
            }

            uint16_t feature_id                 = (static_cast<uint16_t>((*feature_reply)[1]) << 8) | (*feature_reply)[2];
            centurion_sub_features_[feature_id] = CenturionFeatureInfo {
                .index   = index,
                .version = static_cast<uint8_t>(feature_reply->size() > 3 ? (*feature_reply)[3] : 0),
                .flags   = static_cast<uint8_t>(feature_reply->size() > 4 ? (*feature_reply)[4] : 0),
            };
        }

        centurion_features_discovered_ = true;
        return {};
    }

    [[nodiscard]] Result<std::vector<uint8_t>> sendCenturionBridgeRequest(
        hid_device* device_handle,
        uint8_t sub_feature_index,
        uint8_t function,
        std::span<const uint8_t> params = {}) const
    {
        if (!centurion_bridge_index_.has_value()) {
            return DeviceError::protocolError("Centurion bridge index not initialized");
        }

        const auto options = centurionOptions();
        const auto prefix  = centurionFramePrefix();

        auto sub_message              = buildBridgeSubMessageVector(sub_feature_index, function, params);
        const size_t sub_message_size = sub_message.size();
        if (!isBridgeSubMessageSizeSupported(sub_message_size)) {
            return DeviceError::invalidParameter("Centurion bridge message exceeds the 12-bit size limit");
        }

        std::vector<uint8_t> bridge_prefix {
            *centurion_bridge_index_,
            static_cast<uint8_t>(BRIDGE_SEND_FRAGMENT_FN | SOFTWARE_ID),
            static_cast<uint8_t>((sub_message_size >> 8) & 0x0F),
            static_cast<uint8_t>(sub_message_size & 0xFF)
        };

        if (sub_message_size <= MAX_SINGLE_BRIDGE_PAYLOAD) {
            std::vector<uint8_t> layer3 = bridge_prefix;
            layer3.insert(layer3.end(), sub_message.begin(), sub_message.end());

            auto frame = buildCenturionFrame(layer3, 0x00, prefix);
            if (auto write_result = this->writeHID(device_handle, frame, frame.size()); !write_result) {
                return write_result.error();
            }
        } else {
            size_t offset    = 0;
            uint8_t frag_idx = 0;

            while (offset < sub_message_size) {
                const size_t chunk_limit = frag_idx == 0 ? MAX_SINGLE_BRIDGE_PAYLOAD : MAX_CONTINUATION_PAYLOAD;
                const size_t remaining   = sub_message_size - offset;
                const size_t chunk_size  = std::min(chunk_limit, remaining);
                const bool has_more      = (offset + chunk_size) < sub_message_size;
                const uint8_t flags      = static_cast<uint8_t>((frag_idx << 1) | (has_more ? 0x01 : 0x00));

                std::vector<uint8_t> layer3;
                if (frag_idx == 0) {
                    layer3 = bridge_prefix;
                }
                layer3.insert(layer3.end(), sub_message.begin() + static_cast<std::ptrdiff_t>(offset), sub_message.begin() + static_cast<std::ptrdiff_t>(offset + chunk_size));

                auto frame = buildCenturionFrame(layer3, flags, prefix);
                if (auto write_result = this->writeHID(device_handle, frame, frame.size()); !write_result) {
                    return write_result.error();
                }

                offset += chunk_size;
                ++frag_idx;
            }
        }

        bool ack_received = false;
        for (int attempt = 0; attempt < POLL_ATTEMPTS; ++attempt) {
            std::array<uint8_t, FRAME_SIZE> response {};
            auto read_result = this->readHIDTimeout(device_handle, response, hsc_device_timeout);
            if (!read_result) {
                if (ack_received && options.probe_offline_on_timeout
                    && read_result.error().code == DeviceError::Code::Timeout) {
                    return probeOfflineAfterTimeout(device_handle, read_result.error());
                }
                return read_result.error();
            }

            auto payload_result = extractCenturionPayload(response, prefix);
            if (!payload_result) {
                return payload_result.error();
            }

            const auto& reply = *payload_result;
            if (reply.size() < 2 || reply[0] != *centurion_bridge_index_) {
                continue;
            }

            if (options.detect_connection_events && isBridgeConnectionEvent(reply, *centurion_bridge_index_)) {
                if (isBridgeDisconnectedEvent(reply, *centurion_bridge_index_)) {
                    return DeviceError::deviceOffline("Headset is powered off or not connected");
                }
                continue;
            }

            uint8_t func_sw = reply[1];
            if ((func_sw >> 4) != (BRIDGE_MESSAGE_EVENT_FN >> 4)) {
                continue;
            }

            if ((func_sw & 0x0F) == SOFTWARE_ID) {
                ack_received = true;
                continue;
            }

            if ((func_sw & 0x0F) != 0x00) {
                continue;
            }

            if (!isBridgeResponseFor(reply, sub_feature_index)) {
                continue;
            }

            return parseBridgeResponse(reply);
        }

        if (!ack_received) {
            return DeviceError::timeout("Timed out waiting for Centurion bridge acknowledgment");
        }

        auto timeout = DeviceError::timeout("Timed out waiting for Centurion bridge response");
        if (options.probe_offline_on_timeout) {
            return probeOfflineAfterTimeout(device_handle, timeout);
        }
        return timeout;
    }

    /**
     * @brief Tell "headset off" apart from a slow reply after a bridge timeout.
     *
     * With the headset off, the dongle still acknowledges bridge requests, but
     * nothing answers them. Ask the dongle's bridge directly whether a headset
     * is connected; keep the original timeout if the answer is unclear.
     */
    [[nodiscard]] Result<std::vector<uint8_t>> probeOfflineAfterTimeout(
        hid_device* device_handle,
        const DeviceError& timeout) const
    {
        auto status = sendCenturionRequest(device_handle, *centurion_bridge_index_, 0x00, {}, true);
        if (status && isBridgeStatusDisconnected(*status)) {
            return DeviceError::deviceOffline("Headset is powered off or not connected");
        }
        if (!status && status.error().code == DeviceError::Code::DeviceOffline) {
            return status.error();
        }
        return timeout;
    }

    static auto buildDirectPayloadVector(uint8_t feature_index, uint8_t function, std::span<const uint8_t> params)
        -> std::vector<uint8_t>
    {
        std::vector<uint8_t> payload;
        payload.reserve(params.size() + 2);
        payload.push_back(feature_index);
        payload.push_back(static_cast<uint8_t>((function & 0xF0) | SOFTWARE_ID));
        payload.insert(payload.end(), params.begin(), params.end());
        return payload;
    }

    static constexpr auto buildDirectPayload(uint8_t feature_index, uint8_t function, std::span<const uint8_t> params)
        -> std::array<uint8_t, 64>
    {
        std::array<uint8_t, 64> payload {};
        payload[0] = feature_index;
        payload[1] = static_cast<uint8_t>((function & 0xF0) | SOFTWARE_ID);

        for (size_t i = 0; i < params.size() && (i + 2) < payload.size(); ++i) {
            payload[i + 2] = params[i];
        }

        return payload;
    }

    mutable bool centurion_features_discovered_ = false;
    mutable std::optional<uint8_t> centurion_bridge_index_;
    mutable std::unordered_map<uint16_t, CenturionFeatureInfo> centurion_sub_features_;
};

} // namespace headsetcontrol::protocols
