#include "hid_utility.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>

namespace headsetcontrol {

std::optional<std::string> get_hid_path(uint16_t vid, uint16_t pid, int iid, uint16_t usagepageid, uint16_t usageid)
{
    struct hid_device_info* devs = hid_enumerate(vid, pid);

    if (!devs) {
        fprintf(stderr, "HID enumeration failure.\n");
        return std::nullopt;
    }

    std::optional<std::string> result;

    // Because of a MacOS Bug beginning with Ventura 13.3, we ignore the interfaceid
    //   See https://github.com/Sapd/HeadsetControl/issues/281
#ifdef __APPLE__
    iid = 0;
#endif

    // usageid is more specific to interface id, so we try it first.
    // Older hidapi versions on non-Windows did not populate usage_page/usage, so
    // we only trust the match when both values are actually present; otherwise
    // we fall through to the interface_number fallback below.
    if (usageid && usagepageid) // ignore when one of them 0
    {
        struct hid_device_info* cur_dev = devs;
        while (cur_dev) {
            if (cur_dev->usage_page == usagepageid && cur_dev->usage == usageid) {
                result = std::string(cur_dev->path);
                break;
            }

            cur_dev = cur_dev->next;
        }
    }

    if (!result) // only when we didn't yet find something
    {
        struct hid_device_info* cur_dev = devs;
        while (cur_dev) {
            if (!iid || cur_dev->interface_number == iid) {
                result = std::string(cur_dev->path);
                break;
            }

            cur_dev = cur_dev->next;
        }
    }

    hid_free_enumeration(devs);

    return result;
}

void close_hid_device(hid_device* handle)
{
    if (handle) {
        hid_close(handle);
    }
}

} // namespace headsetcontrol
