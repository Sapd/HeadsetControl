#include "device.hpp"

// All three functions generated from CAPABILITIES_XLIST (defined in device.hpp)
// Adding a new capability only requires adding one X() entry to the list.

const char* capability_to_string(capabilities cap)
{
    switch (cap) {
#define X(id, name, short_char) case id: return name;
    CAPABILITIES_XLIST
#undef X
    case NUM_CAPABILITIES: break;
    }
    return "unknown";
}

const char* capability_to_enum_string(capabilities cap)
{
    switch (cap) {
#define X(id, name, short_char) case id: return #id;
    CAPABILITIES_XLIST
#undef X
    case NUM_CAPABILITIES: break;
    }
    return "UNKNOWN";
}

char capability_to_short_char(capabilities cap)
{
    switch (cap) {
#define X(id, name, short_char) case id: return short_char;
    CAPABILITIES_XLIST
#undef X
    case NUM_CAPABILITIES: break;
    }
    return '\0';
}

const char* equalizer_filter_type_to_string(EqualizerFilterType type)
{
    switch (type) {
    case EqualizerFilterType::LowShelf: return "lowshelf";
    case EqualizerFilterType::LowPass: return "lowpass";
    case EqualizerFilterType::Peaking: return "peaking";
    case EqualizerFilterType::HighPass: return "highpass";
    case EqualizerFilterType::HighShelf: return "highshelf";
    case EqualizerFilterType::Notch: return "notch";
    case EqualizerFilterType::Count: break;
    }
    return "unknown";
}

bool has_capability(int capability_mask, capabilities cap)
{
    return (capability_mask & B(cap)) == B(cap);
}
