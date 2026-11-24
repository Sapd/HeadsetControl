#pragma once

#include "devices/hid_device.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace headsetcontrol {

/**
 * @brief A headset's product name, which the device may refine once it is open.
 *
 * Starts as the USB product string from HID enumeration. A device may know more,
 * e.g. which headset is currently paired to a generic dongle, so it is asked once,
 * on the first open handle, as the CLI does. The default getMetadata() returns the
 * HID strings, so devices without such knowledge are unaffected.
 *
 * The enumeration name is never modified and the refined name is assigned at most
 * once, so a view returned by get() stays valid for the lifetime of this object:
 * a view taken before refinement keeps the enumeration name.
 */
class ProductName {
public:
    explicit ProductName(std::string enumeration_name = {})
        : enumeration_name_(std::move(enumeration_name))
    {
    }

    [[nodiscard]] std::string_view get() const
    {
        return refined_name_.empty() ? enumeration_name_ : refined_name_;
    }

    /**
     * @brief Ask the device for its product name, once.
     *
     * Later calls do nothing. An error or an empty name keeps the enumeration name.
     */
    void refine(HIDDevice& device, hid_device* handle)
    {
        if (queried_) {
            return;
        }
        queried_ = true;

        if (auto meta = device.getMetadata(handle); meta && !meta->product.empty()) {
            refined_name_ = std::move(meta->product);
        }
    }

private:
    std::string enumeration_name_;
    std::string refined_name_;
    bool queried_ = false;
};

} // namespace headsetcontrol
