#pragma once

#include "../../device.hpp"
#include "../../result_types.hpp"
#include "../hid_device.hpp"
#include <array>
#include <cstdint>
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
    HeadsetOnboardEQ      = 0x0636,
    HeadsetAudioSidetone  = 0x0604,
};

struct CenturionFeatureInfo {
    uint8_t index   = 0;
    uint8_t version = 0;
    uint8_t flags   = 0;
};

class LogitechCenturionProtocol : public HIDDevice {
protected:
    static constexpr uint8_t REPORT_ID                = 0x51;
    static constexpr size_t FRAME_SIZE                = 64;
    static constexpr uint8_t SOFTWARE_ID              = 0x01;
    static constexpr uint8_t BRIDGE_SEND_FRAGMENT_FN  = 0x10;
    static constexpr uint8_t BRIDGE_MESSAGE_EVENT_FN  = 0x10;
    static constexpr int POLL_ATTEMPTS                = 8;
    static constexpr size_t MAX_SINGLE_BRIDGE_PAYLOAD = 56;
    static constexpr size_t MAX_CONTINUATION_PAYLOAD  = 60;
    static constexpr size_t MAX_BRIDGE_SUB_MESSAGE_SIZE = 0x0FFF;

    [[nodiscard]] Result<std::vector<uint8_t>> sendCenturionRequest(
        hid_device* device_handle,
        uint8_t feature_index,
        uint8_t function,
        std::span<const uint8_t> params = {}) const
    {
        auto payload = buildDirectPayload(feature_index, function, params);
        auto frame   = buildCenturionFrame(payload);

        if (auto write_result = this->writeHID(device_handle, frame, frame.size()); !write_result) {
            return write_result.error();
        }

        for (int attempt = 0; attempt < POLL_ATTEMPTS; ++attempt) {
            std::array<uint8_t, FRAME_SIZE> response {};
            auto read_result = this->readHIDTimeout(device_handle, response, hsc_device_timeout);
            if (!read_result) {
                return read_result.error();
            }

            auto payload_result = extractCenturionPayload(response);
            if (!payload_result) {
                return payload_result.error();
            }

            const auto& reply = *payload_result;
            if (reply.size() < 2) {
                continue;
            }

            if (reply[0] != feature_index) {
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

    static constexpr auto buildCenturionFrame(std::span<const uint8_t> payload, uint8_t flags = 0x00)
        -> std::array<uint8_t, FRAME_SIZE>
    {
        std::array<uint8_t, FRAME_SIZE> frame {};
        frame[0] = REPORT_ID;
        frame[1] = static_cast<uint8_t>(payload.size() + 1);
        frame[2] = flags;

        for (size_t i = 0; i < payload.size() && (i + 3) < frame.size(); ++i) {
            frame[i + 3] = payload[i];
        }

        return frame;
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

        uint8_t sub_cpl       = reply_data[4];
        uint8_t sub_feature   = reply_data[5];
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
                static_cast<uint8_t>(static_cast<uint16_t>(CenturionFeature::FeatureSet) & 0xFF)
            });
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
                static_cast<uint8_t>(static_cast<uint16_t>(CenturionFeature::FeatureSet) & 0xFF)
            });
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

            uint16_t feature_id = (static_cast<uint16_t>((*feature_reply)[1]) << 8) | (*feature_reply)[2];
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

        auto sub_message = buildBridgeSubMessageVector(sub_feature_index, function, params);
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

            auto frame = buildCenturionFrame(layer3);
            if (auto write_result = this->writeHID(device_handle, frame, frame.size()); !write_result) {
                return write_result.error();
            }
        } else {
            size_t offset     = 0;
            uint8_t frag_idx  = 0;

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

                auto frame = buildCenturionFrame(layer3, flags);
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
                return read_result.error();
            }

            auto payload_result = extractCenturionPayload(response);
            if (!payload_result) {
                return payload_result.error();
            }

            const auto& reply = *payload_result;
            if (reply.size() < 2 || reply[0] != *centurion_bridge_index_) {
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

        return DeviceError::timeout("Timed out waiting for Centurion bridge response");
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

    static Result<std::vector<uint8_t>> extractCenturionPayload(std::span<const uint8_t> frame)
    {
        if (frame.size() < 4 || frame[0] != REPORT_ID) {
            return DeviceError::protocolError("Unexpected Centurion frame prefix");
        }

        size_t cpl_length = frame[1];
        if (cpl_length <= 1 || (cpl_length + 2) > frame.size()) {
            return DeviceError::protocolError("Invalid Centurion frame length");
        }

        return std::vector<uint8_t>(frame.begin() + 3, frame.begin() + 2 + cpl_length);
    }

    mutable bool centurion_features_discovered_ = false;
    mutable std::optional<uint8_t> centurion_bridge_index_;
    mutable std::unordered_map<uint16_t, CenturionFeatureInfo> centurion_sub_features_;
};

} // namespace headsetcontrol::protocols