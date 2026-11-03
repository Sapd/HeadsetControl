#pragma once

#include "hid_device.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#ifdef __linux__
#include <chrono>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <poll.h>
#include <termios.h>
#include <thread>
#include <unistd.h>
#endif

using namespace std::string_view_literals;

namespace headsetcontrol {

/**
 * @brief EPOS | Sennheiser GSP 370 wireless gaming headset
 *
 * Communicates via a USB dongle (VID 0x1395, PID 0x009A).
 *
 * The dongle's HID interface (3) only carries media keys. Device settings go
 * over its CDC ACM serial interface (4) at 921600 baud, using packets framed
 * like Bluetooth HCI commands with vendor opcodes (0xFCxx):
 *
 *   Command: 01 <opcode> fc <param length> <params...>
 *   Reply:   04 0e <length> 01 <opcode> fc <status> <values...>
 *
 * The dongle can also send unrelated events (04 e9 00, 04 e8 00), which are
 * skipped while waiting for a reply.
 *
 * The HID interface is only opened because every device needs a HID handle.
 * The handle stays unused, and the serial port is opened per request.
 *
 * Known commands, captured from EPOS Gaming Suite:
 *   0x9C        Read battery, value is the percentage
 *   0xA0        Precedes setting a non-zero sidetone level
 *   0xA1        Precedes setting sidetone level 0
 *   0xA2 level  Set sidetone: 0x00 (off) or 0x06-0x10 (slider steps above 0%)
 *
 * All of them fail with status 0x0C while the headset is switched off.
 *
 * Sidetone is only audible while the mic stream is open. No command for reading
 * the sidetone level is known: 0xA3 kept returning the same value after audible
 * changes.
 */
class SennheiserGSP370 : public HIDDevice {
public:
    static constexpr std::array<uint16_t, 1> PRODUCT_IDS { 0x009A };

    static constexpr uint8_t OPCODE_BATTERY      = 0x9C;
    static constexpr uint8_t OPCODE_SIDETONE_ON  = 0xA0;
    static constexpr uint8_t OPCODE_SIDETONE_OFF = 0xA1;
    static constexpr uint8_t OPCODE_SIDETONE_SET = 0xA2;

    static constexpr uint8_t STATUS_SUCCESS      = 0x00;
    static constexpr uint8_t STATUS_HEADSET_OFF  = 0x0C;

    static constexpr uint8_t SIDETONE_DEVICE_MIN = 0x06;
    static constexpr uint8_t SIDETONE_DEVICE_MAX = 0x10;

    uint16_t getVendorId() const override { return 0x1395; }

    std::vector<uint16_t> getProductIds() const override
    {
        return { PRODUCT_IDS.begin(), PRODUCT_IDS.end() };
    }

    std::string_view getDeviceName() const override { return "Sennheiser GSP 370"sv; }

    constexpr uint8_t getSupportedPlatforms() const override
    {
        // The serial port lookup and access are only implemented for Linux
        return PLATFORM_LINUX;
    }

    constexpr bool usesSerialPort() const override { return true; }

    constexpr int getCapabilities() const override
    {
        return B(CAP_SIDETONE) | B(CAP_BATTERY_STATUS);
    }

    constexpr capability_detail getCapabilityDetail([[maybe_unused]] enum capabilities cap) const override
    {
        return { .usagepage = 0, .usageid = 0, .interface_id = 3 };
    }

    /**
     * @brief Map a sidetone level (0-128) to the device's level
     *
     * Only 0 turns sidetone off. The device has no levels between 0x00 and 0x06.
     */
    [[nodiscard]] static constexpr uint8_t sidetoneDeviceLevel(uint8_t level)
    {
        if (level == 0) {
            return 0;
        }
        int capped = level > 128 ? 128 : level;
        return static_cast<uint8_t>(SIDETONE_DEVICE_MIN + (capped - 1) * (SIDETONE_DEVICE_MAX - SIDETONE_DEVICE_MIN) / 127);
    }

    [[nodiscard]] static std::vector<uint8_t> buildCommand(uint8_t opcode, std::span<const uint8_t> params = {})
    {
        std::vector<uint8_t> packet { 0x01, opcode, 0xFC, static_cast<uint8_t>(params.size()) };
        packet.insert(packet.end(), params.begin(), params.end());
        return packet;
    }

    /**
     * @brief Find the reply to a command in the received bytes
     *
     * @return The status byte followed by the values, or nullopt if the reply
     *         has not been received completely yet
     */
    [[nodiscard]] static std::optional<std::vector<uint8_t>> parseReply(std::span<const uint8_t> received, uint8_t opcode)
    {
        constexpr size_t HEADER_SIZE = 6;
        for (size_t i = 0; i + HEADER_SIZE < received.size(); ++i) {
            bool is_reply = received[i] == 0x04 && received[i + 1] == 0x0E && received[i + 3] == 0x01
                && received[i + 4] == opcode && received[i + 5] == 0xFC;
            if (!is_reply) {
                continue;
            }
            size_t end = i + 3 + received[i + 2];
            if (received[i + 2] < 4 || end > received.size()) {
                return std::nullopt;
            }
            return std::vector<uint8_t>(received.begin() + i + HEADER_SIZE, received.begin() + end);
        }
        return std::nullopt;
    }

    Result<BatteryResult> getBattery([[maybe_unused]] hid_device* device_handle) override
    {
        auto port = SerialPort::open();
        if (!port) {
            return port.error();
        }

        auto reply = port->command(OPCODE_BATTERY);
        if (!reply) {
            return reply.error();
        }
        if (reply->size() != 1 || (*reply)[0] > 100) {
            return DeviceError::protocolError("Unexpected battery reply");
        }

        return BatteryResult {
            .level_percent = (*reply)[0],
            .status        = BATTERY_AVAILABLE,
            .raw_data      = *reply,
        };
    }

    Result<SidetoneResult> setSidetone([[maybe_unused]] hid_device* device_handle, uint8_t level) override
    {
        auto port = SerialPort::open();
        if (!port) {
            return port.error();
        }

        uint8_t device_level = sidetoneDeviceLevel(level);

        auto prepared = port->command(device_level == 0 ? OPCODE_SIDETONE_OFF : OPCODE_SIDETONE_ON);
        if (!prepared) {
            return prepared.error();
        }
        auto set = port->command(OPCODE_SIDETONE_SET, std::array<uint8_t, 1> { device_level });
        if (!set) {
            return set.error();
        }

        return SidetoneResult {
            .current_level = level,
            .min_level     = 0,
            .max_level     = 128,
            .device_min    = 0,
            .device_max    = SIDETONE_DEVICE_MAX,
            .device_level  = device_level,
        };
    }

private:
#ifdef __linux__
    /**
     * @brief The dongle's serial port, open for the lifetime of this object
     */
    class SerialPort {
    public:
        SerialPort(const SerialPort&)            = delete;
        SerialPort& operator=(const SerialPort&) = delete;

        SerialPort(SerialPort&& other) noexcept
            : fd_(other.fd_)
        {
            other.fd_ = -1;
        }

        SerialPort& operator=(SerialPort&&) = delete;

        ~SerialPort()
        {
            if (fd_ >= 0) {
                ::close(fd_);
            }
        }

        static Result<SerialPort> open()
        {
            auto path = findDevice();
            if (!path) {
                return DeviceError::deviceOffline("GSP 370 serial port not found");
            }

            int fd = ::open(path->c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
            if (fd < 0) {
                return DeviceError::hidError("Could not open " + *path + ", check its permissions (headsetcontrol -u prints udev rules)");
            }
            SerialPort port(fd);

            termios settings {};
            if (tcgetattr(fd, &settings) != 0) {
                return DeviceError::hidError("Could not read serial port settings");
            }
            cfmakeraw(&settings);
            settings.c_cflag |= CLOCAL | CREAD;
            settings.c_cflag &= ~CRTSCTS;
            cfsetspeed(&settings, B921600);
            if (tcsetattr(fd, TCSANOW, &settings) != 0) {
                return DeviceError::hidError("Could not apply serial port settings");
            }

            std::this_thread::sleep_for(SETTLE_TIME);
            tcflush(fd, TCIFLUSH);
            return port;
        }

        /**
         * @brief Send a command and wait for its reply
         *
         * @return The values of the reply, without its status byte
         */
        Result<std::vector<uint8_t>> command(uint8_t opcode, std::span<const uint8_t> params = {})
        {
            // The dongle is only known to work with EPOS Gaming Suite's pacing
            std::this_thread::sleep_for(COMMAND_GAP);

            auto packet = buildCommand(opcode, params);
            if (::write(fd_, packet.data(), packet.size()) != static_cast<ssize_t>(packet.size())) {
                return DeviceError::hidError("Could not write to serial port");
            }

            std::vector<uint8_t> received;
            auto deadline = std::chrono::steady_clock::now() + REPLY_TIMEOUT;
            while (true) {
                auto reply = parseReply(received, opcode);
                if (reply) {
                    if (!reply->empty() && (*reply)[0] == STATUS_HEADSET_OFF) {
                        return DeviceError::deviceOffline("Headset is switched off or out of range");
                    }
                    if (reply->empty() || (*reply)[0] != STATUS_SUCCESS) {
                        return DeviceError::protocolError("Command failed");
                    }
                    reply->erase(reply->begin());
                    return *reply;
                }

                auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
                if (remaining.count() <= 0) {
                    return DeviceError::timeout("No reply from the GSP 370 dongle");
                }

                pollfd waiting { .fd = fd_, .events = POLLIN, .revents = 0 };
                int ready = ::poll(&waiting, 1, static_cast<int>(remaining.count()));
                if (ready < 0) {
                    return DeviceError::hidError("Could not wait for serial port");
                }
                if (ready == 0) {
                    continue;
                }

                std::array<uint8_t, 256> buffer {};
                ssize_t count = ::read(fd_, buffer.data(), buffer.size());
                if (count <= 0) {
                    return DeviceError::hidError("Could not read from serial port");
                }
                received.insert(received.end(), buffer.begin(), buffer.begin() + count);
            }
        }

    private:
        static constexpr auto SETTLE_TIME   = std::chrono::milliseconds(500);
        static constexpr auto COMMAND_GAP   = std::chrono::milliseconds(100);
        static constexpr auto REPLY_TIMEOUT = std::chrono::seconds(2);

        explicit SerialPort(int fd)
            : fd_(fd)
        {
        }

        static std::optional<std::string> findDevice()
        {
            namespace fs = std::filesystem;

            std::error_code error;
            for (const auto& entry : fs::directory_iterator("/sys/class/tty", error)) {
                auto interface = fs::canonical(entry.path() / "device", error);
                if (error) {
                    error.clear();
                    continue;
                }
                if (readAttribute(interface.parent_path() / "idVendor") == "1395"
                    && readAttribute(interface.parent_path() / "idProduct") == "009a") {
                    return "/dev/" + entry.path().filename().string();
                }
            }
            return std::nullopt;
        }

        static std::string readAttribute(const std::filesystem::path& path)
        {
            std::ifstream file(path);
            std::string value;
            file >> value;
            return value;
        }

        int fd_;
    };
#else
    class SerialPort {
    public:
        static Result<SerialPort> open()
        {
            return DeviceError::notSupported("The GSP 370 is only supported on Linux");
        }

        Result<std::vector<uint8_t>> command(uint8_t, std::span<const uint8_t> = {})
        {
            return DeviceError::notSupported("The GSP 370 is only supported on Linux");
        }
    };
#endif
};

} // namespace headsetcontrol
