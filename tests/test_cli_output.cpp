/**
 * @file test_cli_output.cpp
 * @brief CLI integration tests for output format validation
 *
 * These tests run the actual headsetcontrol binary with --test-device
 * and validate the output format.
 */

#include <array>
#include <cstdio>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

// Platform-specific executable path
#ifdef _WIN32
#define HEADSETCONTROL_EXE ".\\headsetcontrol.exe"
#else
#define HEADSETCONTROL_EXE "./headsetcontrol"
#endif

namespace headsetcontrol::testing {

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

#define ASSERT_CONTAINS(haystack, needle, msg)                 \
    do {                                                       \
        if ((haystack).find(needle) == std::string::npos) {    \
            std::ostringstream oss;                            \
            oss << "ASSERT_CONTAINS failed: " << (msg) << "\n" \
                << "  Looking for: " << (needle) << "\n"       \
                << "  In output (first 500 chars):\n"          \
                << (haystack).substr(0, 500);                  \
            throw TestFailure(oss.str());                      \
        }                                                      \
    } while (0)

#define ASSERT_NOT_CONTAINS(haystack, needle, msg)                 \
    do {                                                           \
        if ((haystack).find(needle) != std::string::npos) {        \
            std::ostringstream oss;                                \
            oss << "ASSERT_NOT_CONTAINS failed: " << (msg) << "\n" \
                << "  Found (shouldn't be there): " << (needle);   \
            throw TestFailure(oss.str());                          \
        }                                                          \
    } while (0)

// Execute a command and return its output
[[nodiscard]] std::string exec(const char* cmd)
{
    std::array<char, 4096> buffer;
    std::string result;
#ifdef _MSC_VER
    std::unique_ptr<FILE, int (*)(FILE*)> pipe(_popen(cmd, "r"), _pclose);
#else
    std::unique_ptr<FILE, int (*)(FILE*)> pipe(popen(cmd, "r"), pclose);
#endif
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

// ============================================================================
// JSON CLI Tests
// ============================================================================

void testCliJsonOutput()
{
    std::cout << "  Testing CLI JSON output with --test-device..." << std::endl;

    std::string output = exec(HEADSETCONTROL_EXE " --test-device -b -o json 2>&1");

    // Verify JSON structure
    ASSERT_CONTAINS(output, "{", "JSON should start with opening brace");
    ASSERT_CONTAINS(output, "}", "JSON should end with closing brace");

    // Verify required fields
    ASSERT_CONTAINS(output, "\"name\": \"HeadsetControl\"", "Should have name field");
    ASSERT_CONTAINS(output, "\"api_version\":", "Should have api_version");
    ASSERT_CONTAINS(output, "\"device_count\":", "Should have device_count");
    ASSERT_CONTAINS(output, "\"devices\": [", "Should have devices array");

    // Verify test device info
    ASSERT_CONTAINS(output, "\"device\": \"HeadsetControl Test device\"",
        "Should have test device name");
    ASSERT_CONTAINS(output, "\"id_vendor\": \"0xf00b\"", "Should have test vendor ID");
    ASSERT_CONTAINS(output, "\"id_product\": \"0xa00c\"", "Should have test product ID");

    // Verify battery info (test device returns battery)
    ASSERT_CONTAINS(output, "\"battery\":", "Should have battery object");
    ASSERT_CONTAINS(output, "\"level\":", "Should have battery level");
    ASSERT_CONTAINS(output, "\"status\":", "Should have battery status");

    // Verify capabilities
    ASSERT_CONTAINS(output, "\"capabilities\":", "Should have capabilities");
    ASSERT_CONTAINS(output, "CAP_BATTERY_STATUS", "Should have battery capability");
    ASSERT_CONTAINS(output, "CAP_SIDETONE", "Should have sidetone capability");

    std::cout << "    ✓ CLI JSON output is correct" << std::endl;
}

void testCliJsonValid()
{
    std::cout << "  Testing CLI JSON is parseable..." << std::endl;

    std::string output = exec(HEADSETCONTROL_EXE " --test-device -b -o json 2>&1");

    // Basic JSON validation: matching braces and brackets
    int braceCount   = 0;
    int bracketCount = 0;
    bool inString    = false;
    char prevChar    = '\0';

    for (char c : output) {
        if (c == '"' && prevChar != '\\') {
            inString = !inString;
        }
        if (!inString) {
            if (c == '{')
                braceCount++;
            if (c == '}')
                braceCount--;
            if (c == '[')
                bracketCount++;
            if (c == ']')
                bracketCount--;
        }
        prevChar = c;
    }

    ASSERT_TRUE(braceCount == 0, "JSON braces should be balanced");
    ASSERT_TRUE(bracketCount == 0, "JSON brackets should be balanced");

    std::cout << "    ✓ CLI JSON is structurally valid" << std::endl;
}

// ============================================================================
// YAML CLI Tests
// ============================================================================

void testCliYamlOutput()
{
    std::cout << "  Testing CLI YAML output with --test-device..." << std::endl;

    std::string output = exec(HEADSETCONTROL_EXE " --test-device -b -o yaml 2>&1");

    // Verify YAML structure
    ASSERT_CONTAINS(output, "---", "YAML should start with document marker");

    // Verify required fields
    ASSERT_CONTAINS(output, "name: \"HeadsetControl\"", "Should have name field");
    ASSERT_CONTAINS(output, "api_version:", "Should have api_version");
    ASSERT_CONTAINS(output, "device_count:", "Should have device_count");
    ASSERT_CONTAINS(output, "devices:", "Should have devices list");

    // Verify test device
    ASSERT_CONTAINS(output, "- status:", "Should have device with status");
    ASSERT_CONTAINS(output, "HeadsetControl Test device", "Should have test device name");

    // Verify battery
    ASSERT_CONTAINS(output, "battery:", "Should have battery section");
    ASSERT_CONTAINS(output, "level:", "Should have battery level");

    std::cout << "    ✓ CLI YAML output is correct" << std::endl;
}

void testCliYamlIndentation()
{
    std::cout << "  Testing CLI YAML indentation..." << std::endl;

    std::string output = exec(HEADSETCONTROL_EXE " --test-device -b -o yaml 2>&1");

    // Check that device entries use proper YAML list format
    // "- status:" should be followed by "  device:" (aligned)
    size_t statusPos = output.find("- status:");
    ASSERT_TRUE(statusPos != std::string::npos, "Should find '- status:'");

    // Find the line with "device:"
    size_t devicePos = output.find("device:", statusPos);
    ASSERT_TRUE(devicePos != std::string::npos, "Should find 'device:' after status");

    // Get the line containing "device:"
    size_t lineStart       = output.rfind('\n', devicePos) + 1;
    std::string deviceLine = output.substr(lineStart, devicePos - lineStart + 7);

    // Count leading spaces
    size_t leadingSpaces = 0;
    for (char c : deviceLine) {
        if (c == ' ')
            leadingSpaces++;
        else
            break;
    }

    // Get status line position
    size_t statusLineStart = output.rfind('\n', statusPos);
    if (statusLineStart == std::string::npos)
        statusLineStart = 0;
    else
        statusLineStart++;

    size_t statusLeadingSpaces = 0;
    for (size_t i = statusLineStart; i < statusPos && output[i] == ' '; i++) {
        statusLeadingSpaces++;
    }

    // "device:" should be 2 more spaces than "- status:" (to align with "status")
    // status is at position: statusLeadingSpaces + 2 (for "- ")
    // device should be at: statusLeadingSpaces + 2
    size_t expectedDeviceIndent = statusLeadingSpaces + 2;

    ASSERT_TRUE(leadingSpaces == expectedDeviceIndent,
        "device should align with status (accounting for '- ')");

    std::cout << "    ✓ CLI YAML indentation is correct" << std::endl;
}

// ============================================================================
// ENV CLI Tests
// ============================================================================

void testCliEnvOutput()
{
    std::cout << "  Testing CLI ENV output with --test-device..." << std::endl;

    std::string output = exec(HEADSETCONTROL_EXE " --test-device -b -o env 2>&1");

    // Verify ENV format (KEY=value or KEY="value")
    ASSERT_CONTAINS(output, "HEADSETCONTROL_NAME=", "Should have name variable");
    ASSERT_CONTAINS(output, "HEADSETCONTROL_VERSION=", "Should have version variable");
    ASSERT_CONTAINS(output, "DEVICE_COUNT=", "Should have device count");
    ASSERT_CONTAINS(output, "DEVICE_0=", "Should have device 0");

    // Verify test device info
    ASSERT_CONTAINS(output, "HeadsetControl Test device", "Should have test device name");

    // Verify battery info
    ASSERT_CONTAINS(output, "BATTERY_STATUS=", "Should have battery status");
    ASSERT_CONTAINS(output, "BATTERY_LEVEL=", "Should have battery level");

    // Each line should be in VAR=value format (no colons like YAML)
    ASSERT_NOT_CONTAINS(output, ": ", "ENV format should not have YAML-style colons");

    std::cout << "    ✓ CLI ENV output is correct" << std::endl;
}

void testCliEnvShellSafe()
{
    std::cout << "  Testing CLI ENV output is shell-safe..." << std::endl;

    std::string output = exec(HEADSETCONTROL_EXE " --test-device -b -o env 2>&1");

    // Verify keys are uppercase and use underscores
    // Check that variable names only contain valid shell characters
    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.empty() || line[0] == '#')
            continue;

        size_t eqPos = line.find('=');
        if (eqPos == std::string::npos)
            continue;

        std::string key = line.substr(0, eqPos);

        // Key should only contain A-Z, 0-9, _
        for (char c : key) {
            bool valid = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
            ASSERT_TRUE(valid, "ENV variable name should be shell-safe: " + key);
        }
    }

    std::cout << "    ✓ CLI ENV output is shell-safe" << std::endl;
}

// ============================================================================
// Action + Extended Output Tests
// ============================================================================

void testCliActionOnlyOmitsUnrequestedInfo()
{
    std::cout << "  Testing action-only invocation omits info not explicitly requested..." << std::endl;

    // -s (sidetone) is an action; battery/chatmix were not asked for, so an
    // extended-format invocation should not pay for reading them.
    std::string output = exec(HEADSETCONTROL_EXE " --test-device -s 20 -o json 2>&1");

    ASSERT_CONTAINS(output, "\"actions\":", "Should have actions array");
    ASSERT_NOT_CONTAINS(output, "\"battery\":", "Battery info should be omitted when not requested");
    ASSERT_NOT_CONTAINS(output, "\"chatmix\":", "Chatmix info should be omitted when not requested");

    std::cout << "    ✓ Action-only invocation omits unrequested info" << std::endl;
}

void testCliActionWithExplicitInfoKeepsOnlyThat()
{
    std::cout << "  Testing action + explicit info request keeps only that info..." << std::endl;

    // -b is explicitly requested alongside the -s action; chatmix was not
    // asked for and should still be omitted.
    std::string output = exec(HEADSETCONTROL_EXE " --test-device -b -s 20 -o json 2>&1");

    ASSERT_CONTAINS(output, "\"actions\":", "Should have actions array");
    ASSERT_CONTAINS(output, "\"battery\":", "Explicitly requested battery info should be present");
    ASSERT_NOT_CONTAINS(output, "\"chatmix\":", "Chatmix info should be omitted when not requested");

    std::cout << "    ✓ Action with explicit info request keeps only that info" << std::endl;
}

void testCliPureQueryStillReturnsAllInfo()
{
    std::cout << "  Testing pure query (no action) still returns all extended info..." << std::endl;

    // No action requested, so the extended-format default of reading every
    // supported info capability still applies.
    std::string output = exec(HEADSETCONTROL_EXE " --test-device -o json 2>&1");

    ASSERT_CONTAINS(output, "\"battery\":", "Pure query should still report battery");
    ASSERT_CONTAINS(output, "\"chatmix\":", "Pure query should still report chatmix");

    std::cout << "    ✓ Pure query still returns all extended info" << std::endl;
}

// ============================================================================
// Standard Output Tests
// ============================================================================

void testCliStandardOutput()
{
    std::cout << "  Testing CLI standard output with --test-device..." << std::endl;

    std::string output = exec(HEADSETCONTROL_EXE " --test-device -b 2>&1");

    // Standard output should be human-readable
    ASSERT_CONTAINS(output, "Found", "Should indicate device found");
    ASSERT_CONTAINS(output, "Battery", "Should show battery info");
    ASSERT_CONTAINS(output, "HeadsetControl Test device", "Should show device name");

    std::cout << "    ✓ CLI standard output is correct" << std::endl;
}

void testCliStandardBatteryDetails()
{
    std::cout << "  Testing CLI standard output battery details..." << std::endl;

    std::string output = exec(HEADSETCONTROL_EXE " --test-device -b 2>&1");

    // Should show battery details
    ASSERT_CONTAINS(output, "Status:", "Should show battery status label");
    ASSERT_CONTAINS(output, "Level:", "Should show battery level label");
    ASSERT_CONTAINS(output, "%", "Should show percentage");

    // Test device returns extended battery info
    ASSERT_CONTAINS(output, "Voltage:", "Should show voltage");
    ASSERT_CONTAINS(output, "mV", "Should show millivolts unit");

    std::cout << "    ✓ CLI standard battery details are correct" << std::endl;
}

void testCliStandardNoArgs()
{
    std::cout << "  Testing CLI standard output with no args..." << std::endl;

    // When no action is specified, should show version info and hint
    std::string output = exec(HEADSETCONTROL_EXE " --test-device 2>&1");

    // Should show attribution and hint
    ASSERT_CONTAINS(output, "HeadsetControl", "Should show HeadsetControl");
    ASSERT_CONTAINS(output, "Sapd", "Should show author");
    ASSERT_CONTAINS(output, "-h", "Should hint about help");

    std::cout << "    ✓ CLI standard no-args output is correct" << std::endl;
}

void testCliSidetoneStatusOutputs()
{
    std::cout << "  Testing sidetone status in all output formats..." << std::endl;

    const std::string base = HEADSETCONTROL_EXE " --test-device -d 0xf00b:0xa00c -s -o ";

    std::string json = exec((base + "json 2>&1").c_str());
    ASSERT_CONTAINS(json, "\"sidetone\": {", "JSON should have sidetone object");
    ASSERT_CONTAINS(json, "\"level\": 85", "JSON should have normalized level");
    ASSERT_CONTAINS(json, "\"device_level\": 2", "JSON should have native level");
    ASSERT_CONTAINS(json, "\"name\": \"Medium\"", "JSON should have named level");

    std::string yaml = exec((base + "yaml 2>&1").c_str());
    ASSERT_CONTAINS(yaml, "sidetone:", "YAML should have sidetone object");
    ASSERT_CONTAINS(yaml, "device_level: 2", "YAML should have native level");
    ASSERT_CONTAINS(yaml, "name: \"Medium\"", "YAML should have named level");

    std::string env = exec((base + "env 2>&1").c_str());
    ASSERT_CONTAINS(env, "DEVICE_0_SIDETONE_LEVEL=85", "ENV should have normalized level");
    ASSERT_CONTAINS(env, "DEVICE_0_SIDETONE_DEVICE_LEVEL=2", "ENV should have native level");
    ASSERT_CONTAINS(env, "DEVICE_0_SIDETONE_NAME=\"Medium\"", "ENV should have named level");

    std::string standard = exec(HEADSETCONTROL_EXE " --test-device -d 0xf00b:0xa00c -s 2>&1");
    ASSERT_CONTAINS(standard, "Sidetone: Medium (85; device level 2)",
        "standard output should show normalized, native, and named level");

    std::string long_query = exec(HEADSETCONTROL_EXE " --test-device -d 0xf00b:0xa00c --sidetone 2>&1");
    ASSERT_CONTAINS(long_query, "Sidetone: Medium (85; device level 2)",
        "long sidetone option without a value should query");

    std::string short_set = exec(HEADSETCONTROL_EXE " --test-device -d 0xf00b:0xa00c -s 64 2>&1");
    ASSERT_CONTAINS(short_set, "Successfully set sidetone!",
        "short sidetone option with a value should set");

    std::string long_set = exec(HEADSETCONTROL_EXE " --test-device -d 0xf00b:0xa00c --sidetone 64 2>&1");
    ASSERT_CONTAINS(long_set, "Successfully set sidetone!",
        "long sidetone option with a value should set");

    std::string help = exec(HEADSETCONTROL_EXE " --help-all 2>&1");
    ASSERT_CONTAINS(help, "-s, --sidetone [LEVEL]",
        "help should document the optional sidetone level");
    ASSERT_NOT_CONTAINS(help, "--sidetone-status",
        "help should not expose a separate sidetone status option");

    std::cout << "    ✓ Sidetone status output is correct" << std::endl;
}

// ============================================================================
// Short Output Tests
// ============================================================================

void testCliShortOutput()
{
    std::cout << "  Testing CLI short output with --test-device..." << std::endl;

    std::string output = exec(HEADSETCONTROL_EXE " --test-device -b --short-output 2>&1");

    // Short output should be minimal - just a number for battery
    // Should contain a number (battery level)
    bool hasNumber = false;
    for (char c : output) {
        if (c >= '0' && c <= '9') {
            hasNumber = true;
            break;
        }
    }
    ASSERT_TRUE(hasNumber, "Short output should contain battery level number");

    // Should NOT contain verbose text
    ASSERT_NOT_CONTAINS(output, "Found", "Short output should not have verbose text");
    ASSERT_NOT_CONTAINS(output, "Battery:", "Short output should not have labels");

    std::cout << "    ✓ CLI short output is correct" << std::endl;
}

void testCliShortBattery()
{
    std::cout << "  Testing CLI short battery output..." << std::endl;

    // Short output for battery should just be the number
    std::string output = exec(HEADSETCONTROL_EXE " --test-device -b -o short 2>&1");

    // Should contain just a battery level number (42 from test device)
    ASSERT_CONTAINS(output, "42", "Short battery output should show level 42");

    // Should NOT contain verbose labels
    ASSERT_NOT_CONTAINS(output, "Battery:", "Short output should not have labels");
    ASSERT_NOT_CONTAINS(output, "Status:", "Short output should not have labels");

    std::cout << "    ✓ CLI short battery output is correct" << std::endl;
}

void testCliShortDeprecationWarning()
{
    std::cout << "  Testing CLI short output deprecation warning..." << std::endl;

    std::string output = exec(HEADSETCONTROL_EXE " --test-device -b --short-output 2>&1");

    // Short output should include deprecation warning on stderr
    ASSERT_CONTAINS(output, "deprecated", "Short output should show deprecation warning");

    std::cout << "    ✓ CLI short deprecation warning is present" << std::endl;
}

// ============================================================================
// Test Runner
// ============================================================================

void runAllCliOutputTests()
{
    std::cout << "\n╔══════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║           CLI Output Integration Tests               ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════╝\n"
              << std::endl;

    int passed = 0;
    int failed = 0;

    auto runTest = [&](const char* name, void (*test)()) {
        try {
            test();
            passed++;
        } catch (const TestFailure& e) {
            std::cerr << "    ✗ FAILED: " << e.what() << std::endl;
            failed++;
        } catch (const std::exception& e) {
            std::cerr << "    ✗ ERROR: " << e.what() << std::endl;
            failed++;
        }
    };

    std::cout << "=== JSON CLI Tests ===" << std::endl;
    runTest("JSON Output", testCliJsonOutput);
    runTest("JSON Valid Structure", testCliJsonValid);

    std::cout << "\n=== YAML CLI Tests ===" << std::endl;
    runTest("YAML Output", testCliYamlOutput);
    runTest("YAML Indentation", testCliYamlIndentation);

    std::cout << "\n=== ENV CLI Tests ===" << std::endl;
    runTest("ENV Output", testCliEnvOutput);
    runTest("ENV Shell-Safe", testCliEnvShellSafe);

    std::cout << "\n=== Action + Extended Output Tests ===" << std::endl;
    runTest("Action-Only Omits Unrequested Info", testCliActionOnlyOmitsUnrequestedInfo);
    runTest("Action With Explicit Info", testCliActionWithExplicitInfoKeepsOnlyThat);
    runTest("Pure Query Still Returns All Info", testCliPureQueryStillReturnsAllInfo);

    std::cout << "\n=== Standard Output Tests ===" << std::endl;
    runTest("Standard Output", testCliStandardOutput);
    runTest("Standard Battery Details", testCliStandardBatteryDetails);
    runTest("Standard No Args", testCliStandardNoArgs);
    runTest("Sidetone Status Outputs", testCliSidetoneStatusOutputs);

    std::cout << "\n=== Short Output Tests ===" << std::endl;
    runTest("Short Output", testCliShortOutput);
    runTest("Short Battery", testCliShortBattery);
    runTest("Short Deprecation Warning", testCliShortDeprecationWarning);

    std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
    std::cout << "CLI Output Tests: " << passed << " passed, " << failed << " failed" << std::endl;
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;

    if (failed > 0) {
        throw std::runtime_error("Some CLI output tests failed");
    }
}

} // namespace headsetcontrol::testing
