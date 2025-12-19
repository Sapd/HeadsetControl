/**
 * @file test_output_formats.cpp
 * @brief Tests for output format correctness (JSON, YAML, ENV)
 *
 * These tests verify that the output serializers produce valid and
 * correctly formatted output using the --test-device flag.
 */

#include "output/output_data.hpp"
#include "output/serializers.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace headsetcontrol::testing {

using namespace headsetcontrol::output;
using namespace headsetcontrol::serializers;

// ============================================================================
// Test Utilities
// ============================================================================

class TestFailure : public std::runtime_error {
public:
    explicit TestFailure(const std::string& msg)
        : std::runtime_error(msg)
    {
    }
};

#define ASSERT_TRUE(cond, msg)                                              \
    do {                                                                    \
        if (!(cond)) {                                                      \
            throw TestFailure(std::string("ASSERT_TRUE failed: ") + (msg)); \
        }                                                                   \
    } while (0)

#define ASSERT_EQ(expected, actual, msg)                 \
    do {                                                 \
        if ((expected) != (actual)) {                    \
            std::ostringstream oss;                      \
            oss << "ASSERT_EQ failed: " << (msg) << "\n" \
                << "  Expected: " << (expected) << "\n"  \
                << "  Actual:   " << (actual);           \
            throw TestFailure(oss.str());                \
        }                                                \
    } while (0)

#define ASSERT_CONTAINS(haystack, needle, msg)                     \
    do {                                                           \
        if ((haystack).find(needle) == std::string::npos) {        \
            std::ostringstream oss;                                \
            oss << "ASSERT_CONTAINS failed: " << (msg) << "\n"     \
                << "  Looking for: " << (needle) << "\n"           \
                << "  In: " << (haystack).substr(0, 200) << "..."; \
            throw TestFailure(oss.str());                          \
        }                                                          \
    } while (0)

// ============================================================================
// Test Data Builder
// ============================================================================

[[nodiscard]] OutputData createTestOutputData()
{
    OutputData data;
    data.name           = "HeadsetControl";
    data.version        = "1.0.0-test";
    data.api_version    = "1.4";
    data.hidapi_version = "0.15.0";

    DeviceData dev;
    dev.status      = STATUS_SUCCESS;
    dev.vendor_id   = "0x1234";
    dev.product_id  = "0x5678";
    dev.device_name = "Test Headset";
    dev.caps        = { "CAP_BATTERY_STATUS", "CAP_SIDETONE" };
    dev.caps_str    = { "battery", "sidetone" };

    BatteryData bat;
    bat.status     = BATTERY_AVAILABLE;
    bat.level      = 75;
    bat.voltage_mv = 3800;
    dev.battery    = bat;

    dev.chatmix = 64;

    data.devices.push_back(dev);

    return data;
}

// ============================================================================
// JSON Format Tests
// ============================================================================

void testJsonBasicStructure()
{
    std::cout << "  Testing JSON basic structure..." << std::endl;

    std::ostringstream out;
    JsonSerializer s(out);
    auto data = createTestOutputData();

    s.beginDocument();
    s.write("name", data.name);
    s.write("version", data.version);
    s.endDocument();

    std::string result = out.str();

    ASSERT_CONTAINS(result, "{", "JSON should start with opening brace");
    ASSERT_CONTAINS(result, "}", "JSON should end with closing brace");
    ASSERT_CONTAINS(result, "\"name\": \"HeadsetControl\"", "Should contain name field");
    ASSERT_CONTAINS(result, "\"version\": \"1.0.0-test\"", "Should contain version field");

    std::cout << "    ✓ JSON basic structure is correct" << std::endl;
}

void testJsonArrayFormat()
{
    std::cout << "  Testing JSON array format..." << std::endl;

    std::ostringstream out;
    JsonSerializer s(out);

    s.beginDocument();
    s.beginArray("items");
    s.writeArrayItem("first");
    s.writeArrayItem("second");
    s.writeArrayItem(42);
    s.endArray();
    s.endDocument();

    std::string result = out.str();

    ASSERT_CONTAINS(result, "\"items\": [", "Should have array opening");
    ASSERT_CONTAINS(result, "\"first\"", "Should contain first item");
    ASSERT_CONTAINS(result, "\"second\"", "Should contain second item");
    ASSERT_CONTAINS(result, "42", "Should contain integer item");
    ASSERT_CONTAINS(result, "]", "Should have array closing");

    std::cout << "    ✓ JSON array format is correct" << std::endl;
}

void testJsonNestedObjects()
{
    std::cout << "  Testing JSON nested objects..." << std::endl;

    std::ostringstream out;
    JsonSerializer s(out);

    s.beginDocument();
    s.beginObject("battery");
    s.write("level", 75);
    s.write("status", "available");
    s.endObject();
    s.endDocument();

    std::string result = out.str();

    ASSERT_CONTAINS(result, "\"battery\": {", "Should have nested object");
    ASSERT_CONTAINS(result, "\"level\": 75", "Should contain level in nested object");
    ASSERT_CONTAINS(result, "\"status\": \"available\"", "Should contain status");

    std::cout << "    ✓ JSON nested objects are correct" << std::endl;
}

void testJsonEscaping()
{
    std::cout << "  Testing JSON string escaping..." << std::endl;

    std::ostringstream out;
    JsonSerializer s(out);

    s.beginDocument();
    s.write("message", "Hello \"World\"\nNew line");
    s.endDocument();

    std::string result = out.str();

    ASSERT_CONTAINS(result, "\\\"World\\\"", "Should escape quotes");
    ASSERT_CONTAINS(result, "\\n", "Should escape newlines");

    std::cout << "    ✓ JSON escaping is correct" << std::endl;
}

// ============================================================================
// YAML Format Tests
// ============================================================================

void testYamlBasicStructure()
{
    std::cout << "  Testing YAML basic structure..." << std::endl;

    std::ostringstream out;
    YamlSerializer s(out);

    s.beginDocument();
    s.write("name", "HeadsetControl");
    s.write("version", "1.0.0");
    s.endDocument();

    std::string result = out.str();

    ASSERT_CONTAINS(result, "---", "YAML should start with document marker");
    ASSERT_CONTAINS(result, "name: \"HeadsetControl\"", "Should contain name field");
    ASSERT_CONTAINS(result, "version: \"1.0.0\"", "Should contain version field");

    std::cout << "    ✓ YAML basic structure is correct" << std::endl;
}

void testYamlArrayFormat()
{
    std::cout << "  Testing YAML array format..." << std::endl;

    std::ostringstream out;
    YamlSerializer s(out);

    s.beginDocument();
    s.beginArray("capabilities");
    s.writeArrayItem("battery");
    s.writeArrayItem("sidetone");
    s.endArray();
    s.endDocument();

    std::string result = out.str();

    ASSERT_CONTAINS(result, "capabilities:", "Should have array key");
    ASSERT_CONTAINS(result, "- battery", "Should have first item with dash");
    ASSERT_CONTAINS(result, "- sidetone", "Should have second item with dash");

    std::cout << "    ✓ YAML array format is correct" << std::endl;
}

void testYamlArrayOfObjectsIndentation()
{
    std::cout << "  Testing YAML array of objects indentation..." << std::endl;

    std::ostringstream out;
    YamlSerializer s(out);

    s.beginDocument();
    s.beginArray("devices");

    // First device - using writeListItem like outputYaml does
    s.writeListItem("status", "success");
    s.pushIndent(1);
    s.write("device", "Test Device 1");
    s.write("id", "0x1234");
    s.popIndent(1);

    // Second device
    s.writeListItem("status", "partial");
    s.pushIndent(1);
    s.write("device", "Test Device 2");
    s.write("id", "0x5678");
    s.popIndent(1);

    s.endArray();
    s.endDocument();

    std::string result = out.str();

    // Check proper indentation: "- status" then "  device" aligned
    // The "-" is at indent level, "status" starts after "- "
    // "device" should be at same position as "status" (using 2 extra spaces)

    // Split into lines and check indentation
    std::istringstream iss(result);
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(iss, line)) {
        lines.push_back(line);
    }

    bool found_first_status = false;
    bool found_first_device = false;
    size_t status_indent    = 0;
    size_t device_indent    = 0;

    for (const auto& l : lines) {
        if (!found_first_status && l.find("- status:") != std::string::npos) {
            found_first_status = true;
            status_indent      = l.find('-');
        }
        if (found_first_status && !found_first_device && l.find("device:") != std::string::npos) {
            found_first_device = true;
            device_indent      = l.find('d');
        }
    }

    ASSERT_TRUE(found_first_status, "Should find '- status:' line");
    ASSERT_TRUE(found_first_device, "Should find 'device:' line");

    // "device" should align with "status" (status_indent + 2 for "- ")
    size_t expected_device_indent = status_indent + 2;
    ASSERT_EQ(expected_device_indent, device_indent,
        "device should align with status (after '- ')");

    std::cout << "    ✓ YAML array of objects indentation is correct" << std::endl;
}

void testYamlNestedArrays()
{
    std::cout << "  Testing YAML nested arrays..." << std::endl;

    std::ostringstream out;
    YamlSerializer s(out);

    s.beginDocument();
    s.beginArray("devices");
    s.writeListItem("name", "Device1");
    s.pushIndent(1);

    s.beginArray("capabilities");
    s.writeArrayItem("battery");
    s.writeArrayItem("sidetone");
    s.endArray();

    s.popIndent(1);
    s.endArray();
    s.endDocument();

    std::string result = out.str();

    ASSERT_CONTAINS(result, "devices:", "Should have devices array");
    ASSERT_CONTAINS(result, "- name:", "Should have device with name");
    ASSERT_CONTAINS(result, "capabilities:", "Should have nested capabilities");
    ASSERT_CONTAINS(result, "- battery", "Should have battery capability");

    std::cout << "    ✓ YAML nested arrays are correct" << std::endl;
}

// ============================================================================
// ENV Format Tests
// ============================================================================

void testEnvBasicFormat()
{
    std::cout << "  Testing ENV basic format..." << std::endl;

    std::ostringstream out;
    EnvSerializer s(out);

    s.write("HEADSETCONTROL_NAME", "HeadsetControl");
    s.write("HEADSETCONTROL_VERSION", "1.0.0");
    s.write("DEVICE_COUNT", 1);

    std::string result = out.str();

    ASSERT_CONTAINS(result, "HEADSETCONTROL_NAME=\"HeadsetControl\"",
        "Should have name in ENV format");
    ASSERT_CONTAINS(result, "HEADSETCONTROL_VERSION=\"1.0.0\"",
        "Should have version in ENV format");
    ASSERT_CONTAINS(result, "DEVICE_COUNT=1",
        "Should have device count (no quotes for int)");

    std::cout << "    ✓ ENV basic format is correct" << std::endl;
}

void testEnvKeyFormatting()
{
    std::cout << "  Testing ENV key formatting..." << std::endl;

    std::ostringstream out;
    EnvSerializer s(out);

    s.write("battery status", "available");
    s.write("device-name", "Test");

    std::string result = out.str();

    ASSERT_CONTAINS(result, "BATTERY_STATUS=", "Spaces should become underscores");
    ASSERT_CONTAINS(result, "DEVICE_NAME=", "Dashes should become underscores");

    std::cout << "    ✓ ENV key formatting is correct" << std::endl;
}

// ============================================================================
// Integration Tests with OutputData
// ============================================================================

void testFullJsonOutput()
{
    std::cout << "  Testing full JSON output with OutputData..." << std::endl;

    std::ostringstream out;
    JsonSerializer s(out);
    auto data = createTestOutputData();
    data.serialize(s);

    std::string result = out.str();

    // Verify structure
    ASSERT_CONTAINS(result, "\"name\": \"HeadsetControl\"", "Should have name");
    ASSERT_CONTAINS(result, "\"devices\": [", "Should have devices array");
    ASSERT_CONTAINS(result, "\"device\": \"Test Headset\"", "Should have device name");
    ASSERT_CONTAINS(result, "\"battery\": {", "Should have battery object");
    ASSERT_CONTAINS(result, "\"level\": 75", "Should have battery level");
    ASSERT_CONTAINS(result, "\"chatmix\": 64", "Should have chatmix");

    std::cout << "    ✓ Full JSON output is correct" << std::endl;
}

void testBatteryDataSerialization()
{
    std::cout << "  Testing BatteryData serialization..." << std::endl;

    BatteryData bat;
    bat.status           = BATTERY_CHARGING;
    bat.level            = 50;
    bat.voltage_mv       = 4100;
    bat.time_to_full_min = 30;

    // Test JSON
    {
        std::ostringstream out;
        JsonSerializer s(out);
        s.beginDocument();
        bat.serialize(s);
        s.endDocument();

        std::string result = out.str();
        ASSERT_CONTAINS(result, "\"status\": \"BATTERY_CHARGING\"", "Should have charging status");
        ASSERT_CONTAINS(result, "\"level\": 50", "Should have level");
        ASSERT_CONTAINS(result, "\"voltage_mv\": 4100", "Should have voltage");
        ASSERT_CONTAINS(result, "\"time_to_full_min\": 30", "Should have time to full");
    }

    // Test YAML
    {
        std::ostringstream out;
        YamlSerializer s(out);
        s.beginDocument();
        bat.serialize(s);
        s.endDocument();

        std::string result = out.str();
        ASSERT_CONTAINS(result, "battery:", "Should have battery key");
        ASSERT_CONTAINS(result, "status: \"BATTERY_CHARGING\"", "Should have status");
        ASSERT_CONTAINS(result, "level: 50", "Should have level");
    }

    std::cout << "    ✓ BatteryData serialization is correct" << std::endl;
}

// ============================================================================
// Test Runner
// ============================================================================

void runAllOutputFormatTests()
{
    std::cout << "\n╔══════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║           Output Format Tests                        ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════╝\n"
              << std::endl;

    int passed = 0;
    int failed = 0;

    auto runTest = [&]([[maybe_unused]] const char* name, void (*test)()) {
        try {
            test();
            passed++;
        } catch (const TestFailure& e) {
            std::cerr << "    ✗ FAILED: " << e.what() << std::endl;
            failed++;
        }
    };

    std::cout << "=== JSON Format Tests ===" << std::endl;
    runTest("JSON Basic Structure", testJsonBasicStructure);
    runTest("JSON Array Format", testJsonArrayFormat);
    runTest("JSON Nested Objects", testJsonNestedObjects);
    runTest("JSON Escaping", testJsonEscaping);

    std::cout << "\n=== YAML Format Tests ===" << std::endl;
    runTest("YAML Basic Structure", testYamlBasicStructure);
    runTest("YAML Array Format", testYamlArrayFormat);
    runTest("YAML Array of Objects Indentation", testYamlArrayOfObjectsIndentation);
    runTest("YAML Nested Arrays", testYamlNestedArrays);

    std::cout << "\n=== ENV Format Tests ===" << std::endl;
    runTest("ENV Basic Format", testEnvBasicFormat);
    runTest("ENV Key Formatting", testEnvKeyFormatting);

    std::cout << "\n=== Integration Tests ===" << std::endl;
    runTest("Full JSON Output", testFullJsonOutput);
    runTest("BatteryData Serialization", testBatteryDataSerialization);

    std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
    std::cout << "Output Format Tests: " << passed << " passed, " << failed << " failed" << std::endl;
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;

    if (failed > 0) {
        throw std::runtime_error("Some output format tests failed");
    }
}

} // namespace headsetcontrol::testing
