#include "result_types.hpp"
#include <format>

namespace headsetcontrol {

std::string DeviceError::fullMessage() const
{
    std::string result = message;

    if (!details.empty()) {
        result += ": " + details;
    }

    // Add location info
    result += std::format(" (at {}:{})",
        location.file_name(),
        location.line());

    return result;
}

DeviceError DeviceError::timeout(std::string_view details, std::source_location loc)
{
    return DeviceError {
        .code     = Code::Timeout,
        .message  = "Operation timed out",
        .details  = std::string(details),
        .location = loc
    };
}

DeviceError DeviceError::deviceOffline(std::string_view details, std::source_location loc)
{
    return DeviceError {
        .code     = Code::DeviceOffline,
        .message  = "Device is offline or not responding",
        .details  = std::string(details),
        .location = loc
    };
}

DeviceError DeviceError::protocolError(std::string_view details, std::source_location loc)
{
    return DeviceError {
        .code     = Code::ProtocolError,
        .message  = "Protocol error",
        .details  = std::string(details),
        .location = loc
    };
}

DeviceError DeviceError::invalidParameter(std::string_view details, std::source_location loc)
{
    return DeviceError {
        .code     = Code::InvalidParameter,
        .message  = "Invalid parameter",
        .details  = std::string(details),
        .location = loc
    };
}

DeviceError DeviceError::notSupported(std::string_view details, std::source_location loc)
{
    return DeviceError {
        .code     = Code::NotSupported,
        .message  = "Feature not supported by this device",
        .details  = std::string(details),
        .location = loc
    };
}

DeviceError DeviceError::hidError(std::string_view details, std::source_location loc)
{
    return DeviceError {
        .code     = Code::HIDError,
        .message  = "HID communication error",
        .details  = std::string(details),
        .location = loc
    };
}

} // namespace headsetcontrol
