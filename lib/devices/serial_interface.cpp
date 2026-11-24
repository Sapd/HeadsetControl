#include "serial_interface.hpp"

#include "../hid_utility.hpp"

#include <format>
#include <optional>

#ifdef __linux__
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <thread>
#include <unistd.h>
#endif

namespace headsetcontrol {

#ifdef __linux__

namespace {

    std::optional<speed_t> toSpeed(uint32_t baud_rate)
    {
        switch (baud_rate) {
        case 9600:
            return B9600;
        case 19200:
            return B19200;
        case 38400:
            return B38400;
        case 57600:
            return B57600;
        case 115200:
            return B115200;
        case 230400:
            return B230400;
        case 460800:
            return B460800;
        case 921600:
            return B921600;
        default:
            return std::nullopt;
        }
    }

} // namespace

auto RealSerialInterface::open(uint16_t vid, uint16_t pid, uint32_t baud_rate,
    std::chrono::milliseconds settle_time) -> Result<int>
{
    auto speed = toSpeed(baud_rate);
    if (!speed) {
        return DeviceError::invalidParameter(std::format("Unsupported baud rate {}", baud_rate));
    }

    auto path = get_serial_path(vid, pid);
    if (!path) {
        return DeviceError::deviceOffline("Serial port not found");
    }

    int fd = ::open(path->c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        return DeviceError::hidError(
            std::format("Could not open {}, check its permissions (headsetcontrol -u prints udev rules)", *path));
    }

    termios settings {};
    if (tcgetattr(fd, &settings) != 0) {
        ::close(fd);
        return DeviceError::hidError("Could not read serial port settings");
    }
    cfmakeraw(&settings);
    settings.c_cflag |= CLOCAL | CREAD;
    settings.c_cflag &= ~CRTSCTS;
    cfsetspeed(&settings, *speed);
    if (tcsetattr(fd, TCSANOW, &settings) != 0) {
        ::close(fd);
        return DeviceError::hidError("Could not apply serial port settings");
    }

    std::this_thread::sleep_for(settle_time);
    tcflush(fd, TCIFLUSH);
    return fd;
}

void RealSerialInterface::close(int handle)
{
    if (handle >= 0) {
        ::close(handle);
    }
}

auto RealSerialInterface::write(int handle, std::span<const uint8_t> data) -> Result<void>
{
    const auto bytes_written = ::write(handle, data.data(), data.size());
    if (bytes_written != static_cast<ssize_t>(data.size())) [[unlikely]] {
        return DeviceError::hidError(
            std::format("Serial write failed: {} bytes, expected {}", bytes_written, data.size()));
    }
    return {};
}

auto RealSerialInterface::readTimeout(int handle, std::span<uint8_t> data, int timeout_ms) -> Result<size_t>
{
    pollfd waiting { .fd = handle, .events = POLLIN, .revents = 0 };
    const int ready = ::poll(&waiting, 1, timeout_ms);
    if (ready < 0) [[unlikely]] {
        return DeviceError::hidError("Could not wait for serial port");
    }
    if (ready == 0) {
        return DeviceError::timeout(std::format("Serial read timeout after {}ms (no data)", timeout_ms));
    }

    const auto bytes_read = ::read(handle, data.data(), data.size());
    if (bytes_read <= 0) [[unlikely]] {
        return DeviceError::hidError("Serial read failed");
    }
    return static_cast<size_t>(bytes_read);
}

#else

auto RealSerialInterface::open(uint16_t, uint16_t, uint32_t, std::chrono::milliseconds) -> Result<int>
{
    return DeviceError::notSupported("Serial ports are only supported on Linux for now");
}

void RealSerialInterface::close(int) { }

auto RealSerialInterface::write(int, std::span<const uint8_t>) -> Result<void>
{
    return DeviceError::notSupported("Serial ports are only supported on Linux for now");
}

auto RealSerialInterface::readTimeout(int, std::span<uint8_t>, int) -> Result<size_t>
{
    return DeviceError::notSupported("Serial ports are only supported on Linux for now");
}

#endif

} // namespace headsetcontrol
