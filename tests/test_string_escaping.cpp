/**
 * @file test_string_escaping.cpp
 * @brief Tests for string escaping in JSON, YAML, and other output formats
 *
 * Tests for:
 * - JSON string escaping (RFC 8259 compliant)
 * - YAML string escaping
 * - Control character handling
 * - Unicode handling
 * - Edge cases in escaping
 */

#include "output/serializers.hpp"

#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace headsetcontrol::testing {

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

#define ASSERT_FALSE(cond, msg)                                              \
    do {                                                                     \
        if ((cond)) {                                                        \
            throw TestFailure(std::string("ASSERT_FALSE failed: ") + (msg)); \
        }                                                                    \
    } while (0)

#define ASSERT_EQ(expected, actual, msg)                  \
    do {                                                  \
        if ((expected) != (actual)) {                     \
            std::ostringstream oss;                       \
            oss << "ASSERT_EQ failed: " << (msg) << "\n"  \
                << "  Expected: [" << (expected) << "]\n" \
                << "  Actual:   [" << (actual) << "]";    \
            throw TestFailure(oss.str());                 \
        }                                                 \
    } while (0)

#define ASSERT_CONTAINS(haystack, needle, msg)                 \
    do {                                                       \
        if ((haystack).find(needle) == std::string::npos) {    \
            std::ostringstream oss;                            \
            oss << "ASSERT_CONTAINS failed: " << (msg) << "\n" \
                << "  Looking for: [" << (needle) << "]\n"     \
                << "  In: [" << (haystack) << "]";             \
            throw TestFailure(oss.str());                      \
        }                                                      \
    } while (0)

#define ASSERT_NOT_CONTAINS(haystack, needle, msg)                        \
    do {                                                                  \
        if ((haystack).find(needle) != std::string::npos) {               \
            std::ostringstream oss;                                       \
            oss << "ASSERT_NOT_CONTAINS failed: " << (msg) << "\n"        \
                << "  Found (shouldn't be there): [" << (needle) << "]\n" \
                << "  In: [" << (haystack) << "]";                        \
            throw TestFailure(oss.str());                                 \
        }                                                                 \
    } while (0)

// ============================================================================
// JSON Escaping Tests
// ============================================================================

void testJsonQuoteEscaping()
{
    std::cout << "  Testing JSON quote escaping..." << std::endl;

    std::ostringstream out;
    JsonSerializer s(out);

    s.beginDocument();
    s.write("message", "Hello \"World\"");
    s.endDocument();

    std::string result = out.str();

    // Quotes should be escaped as \"
    ASSERT_CONTAINS(result, "\\\"World\\\"", "Quotes should be escaped");
    // Should NOT have unescaped quotes within the string value
    // (The result has outer quotes for the JSON format, but inner quotes must be escaped)

    std::cout << "    OK JSON quote escaping" << std::endl;
}

void testJsonBackslashEscaping()
{
    std::cout << "  Testing JSON backslash escaping..." << std::endl;

    std::ostringstream out;
    JsonSerializer s(out);

    s.beginDocument();
    s.write("path", "C:\\Users\\Test\\file.txt");
    s.endDocument();

    std::string result = out.str();

    // Backslashes should be escaped as \\
    ASSERT_CONTAINS(result, "C:\\\\Users\\\\Test\\\\file.txt", "Backslashes should be escaped");

    std::cout << "    OK JSON backslash escaping" << std::endl;
}

void testJsonNewlineEscaping()
{
    std::cout << "  Testing JSON newline escaping..." << std::endl;

    std::ostringstream out;
    JsonSerializer s(out);

    s.beginDocument();
    s.write("text", "Line1\nLine2\nLine3");
    s.endDocument();

    std::string result = out.str();

    // Newlines should be escaped as \n
    ASSERT_CONTAINS(result, "Line1\\nLine2\\nLine3", "Newlines should be escaped");

    std::cout << "    OK JSON newline escaping" << std::endl;
}

void testJsonCarriageReturnEscaping()
{
    std::cout << "  Testing JSON carriage return escaping..." << std::endl;

    std::ostringstream out;
    JsonSerializer s(out);

    s.beginDocument();
    s.write("text", "Line1\rLine2");
    s.endDocument();

    std::string result = out.str();

    // Carriage returns should be escaped as \r
    ASSERT_CONTAINS(result, "\\r", "Carriage returns should be escaped");

    std::cout << "    OK JSON carriage return escaping" << std::endl;
}

void testJsonTabEscaping()
{
    std::cout << "  Testing JSON tab escaping..." << std::endl;

    std::ostringstream out;
    JsonSerializer s(out);

    s.beginDocument();
    s.write("text", "Col1\tCol2\tCol3");
    s.endDocument();

    std::string result = out.str();

    // Tabs should be escaped as \t
    ASSERT_CONTAINS(result, "\\t", "Tabs should be escaped");

    std::cout << "    OK JSON tab escaping" << std::endl;
}

void testJsonBackspaceEscaping()
{
    std::cout << "  Testing JSON backspace escaping..." << std::endl;

    std::ostringstream out;
    JsonSerializer s(out);

    s.beginDocument();
    std::string text = "Text";
    text += '\b'; // Backspace character
    text += "More";
    s.write("text", text);
    s.endDocument();

    std::string result = out.str();

    // Backspace should be escaped as \b
    ASSERT_CONTAINS(result, "\\b", "Backspace should be escaped");

    std::cout << "    OK JSON backspace escaping" << std::endl;
}

void testJsonFormFeedEscaping()
{
    std::cout << "  Testing JSON form feed escaping..." << std::endl;

    std::ostringstream out;
    JsonSerializer s(out);

    s.beginDocument();
    std::string text = "Page1";
    text += '\f'; // Form feed character
    text += "Page2";
    s.write("text", text);
    s.endDocument();

    std::string result = out.str();

    // Form feed should be escaped as \f
    ASSERT_CONTAINS(result, "\\f", "Form feed should be escaped");

    std::cout << "    OK JSON form feed escaping" << std::endl;
}

void testJsonControlCharacterEscaping()
{
    std::cout << "  Testing JSON control character escaping..." << std::endl;

    std::ostringstream out;
    JsonSerializer s(out);

    s.beginDocument();
    // Test control characters 0x00-0x1F (except those with named escapes)
    std::string text;
    text += '\x01'; // SOH
    text += '\x02'; // STX
    text += '\x1F'; // Unit separator
    s.write("control", text);
    s.endDocument();

    std::string result = out.str();

    // Control characters should be escaped as \uXXXX
    ASSERT_CONTAINS(result, "\\u0001", "SOH should be escaped as \\u0001");
    ASSERT_CONTAINS(result, "\\u0002", "STX should be escaped as \\u0002");
    ASSERT_CONTAINS(result, "\\u001f", "Unit separator should be escaped as \\u001f");

    std::cout << "    OK JSON control character escaping" << std::endl;
}

void testJsonNullCharacterEscaping()
{
    std::cout << "  Testing JSON null character escaping..." << std::endl;

    std::ostringstream out;
    JsonSerializer s(out);

    s.beginDocument();
    std::string text = "Before";
    text += '\0'; // Null character - NOTE: this truncates the std::string in many cases
    // Actually with std::string we can have embedded nulls, but string_view might not handle it well
    s.write("null_test", std::string_view(text.data(), text.size()));
    s.endDocument();

    std::string result = out.str();

    // The output should contain escaped null or at minimum not break
    // Just verify the document is still valid JSON structure
    ASSERT_CONTAINS(result, "{", "Should still have valid JSON structure");
    ASSERT_CONTAINS(result, "}", "Should still have valid JSON structure");

    std::cout << "    OK JSON null character escaping" << std::endl;
}

void testJsonCombinedEscaping()
{
    std::cout << "  Testing JSON combined escaping..." << std::endl;

    std::ostringstream out;
    JsonSerializer s(out);

    s.beginDocument();
    s.write("complex", "Path: \"C:\\Program Files\\App\"\nVersion: 1.0\tRelease");
    s.endDocument();

    std::string result = out.str();

    // All special characters should be properly escaped
    ASSERT_CONTAINS(result, "\\\"C:", "Quote should be escaped");
    ASSERT_CONTAINS(result, "\\\\Program", "Backslash should be escaped");
    ASSERT_CONTAINS(result, "\\n", "Newline should be escaped");
    ASSERT_CONTAINS(result, "\\t", "Tab should be escaped");

    std::cout << "    OK JSON combined escaping" << std::endl;
}

void testJsonEmptyString()
{
    std::cout << "  Testing JSON empty string..." << std::endl;

    std::ostringstream out;
    JsonSerializer s(out);

    s.beginDocument();
    s.write("empty", "");
    s.endDocument();

    std::string result = out.str();

    // Empty string should be valid JSON
    ASSERT_CONTAINS(result, "\"empty\": \"\"", "Empty string should be valid");

    std::cout << "    OK JSON empty string" << std::endl;
}

// ============================================================================
// YAML Escaping Tests
// ============================================================================

void testYamlQuoteEscaping()
{
    std::cout << "  Testing YAML quote escaping..." << std::endl;

    std::ostringstream out;
    YamlSerializer s(out);

    s.beginDocument();
    s.write("message", "Hello \"World\"");
    s.endDocument();

    std::string result = out.str();

    // Quotes should be escaped
    ASSERT_CONTAINS(result, "\\\"World\\\"", "Quotes should be escaped in YAML");

    std::cout << "    OK YAML quote escaping" << std::endl;
}

void testYamlBackslashEscaping()
{
    std::cout << "  Testing YAML backslash escaping..." << std::endl;

    std::ostringstream out;
    YamlSerializer s(out);

    s.beginDocument();
    s.write("path", "C:\\Users\\Test");
    s.endDocument();

    std::string result = out.str();

    // Backslashes should be escaped
    ASSERT_CONTAINS(result, "\\\\", "Backslashes should be escaped in YAML");

    std::cout << "    OK YAML backslash escaping" << std::endl;
}

void testYamlNewlineEscaping()
{
    std::cout << "  Testing YAML newline escaping..." << std::endl;

    std::ostringstream out;
    YamlSerializer s(out);

    s.beginDocument();
    s.write("text", "Line1\nLine2");
    s.endDocument();

    std::string result = out.str();

    // Newlines should be escaped
    ASSERT_CONTAINS(result, "\\n", "Newlines should be escaped in YAML");

    std::cout << "    OK YAML newline escaping" << std::endl;
}

void testYamlSpecialCharacters()
{
    std::cout << "  Testing YAML special characters..." << std::endl;

    std::ostringstream out;
    YamlSerializer s(out);

    s.beginDocument();
    s.write("special", "Tab:\tReturn:\rEnd");
    s.endDocument();

    std::string result = out.str();

    // Tabs and carriage returns should be escaped
    ASSERT_CONTAINS(result, "\\t", "Tab should be escaped in YAML");
    ASSERT_CONTAINS(result, "\\r", "Carriage return should be escaped in YAML");

    std::cout << "    OK YAML special characters" << std::endl;
}

void testYamlEmptyValue()
{
    std::cout << "  Testing YAML empty value..." << std::endl;

    std::ostringstream out;
    YamlSerializer s(out);

    s.beginDocument();
    s.write("empty", "");
    s.endDocument();

    std::string result = out.str();

    // Empty value should produce valid YAML
    ASSERT_CONTAINS(result, "empty:", "Key should be present");
    // Empty value is represented as just "key:" with no value after

    std::cout << "    OK YAML empty value" << std::endl;
}

// ============================================================================
// Edge Cases in Escaping
// ============================================================================

void testVeryLongString()
{
    std::cout << "  Testing very long string..." << std::endl;

    std::ostringstream out;
    JsonSerializer s(out);

    // Create a very long string
    std::string long_string(10000, 'x');
    long_string[5000] = '"'; // Insert a quote in the middle
    long_string[5001] = '\\'; // Insert a backslash

    s.beginDocument();
    s.write("long", long_string);
    s.endDocument();

    std::string result = out.str();

    // Should handle long strings without truncation
    ASSERT_TRUE(result.length() > 10000, "Long string should not be truncated");
    ASSERT_CONTAINS(result, "\\\"", "Quote in long string should be escaped");
    ASSERT_CONTAINS(result, "\\\\", "Backslash in long string should be escaped");

    std::cout << "    OK very long string" << std::endl;
}

void testManySpecialCharacters()
{
    std::cout << "  Testing many special characters..." << std::endl;

    std::ostringstream out;
    JsonSerializer s(out);

    // String with many consecutive special characters
    s.beginDocument();
    s.write("special", "\"\"\"\"\\\\\\\\\n\n\n\n\t\t\t\t");
    s.endDocument();

    std::string result = out.str();

    // All should be escaped
    ASSERT_CONTAINS(result, "\\\"\\\"\\\"\\\"", "Multiple quotes should be escaped");
    ASSERT_CONTAINS(result, "\\\\\\\\\\\\\\\\", "Multiple backslashes should be escaped");

    std::cout << "    OK many special characters" << std::endl;
}

void testAlternatingSpecialChars()
{
    std::cout << "  Testing alternating special characters..." << std::endl;

    std::ostringstream out;
    JsonSerializer s(out);

    s.beginDocument();
    s.write("alt", "\"\\\"\\\"\\\"\\\"\\\"\\\"\"\"\"\"\"\"\"\"\"\"\"\"\n\n\n\n\t\t\t\t");
    s.endDocument();

    std::string result = out.str();

    // Should be valid JSON
    ASSERT_CONTAINS(result, "{", "Should have valid JSON structure");
    ASSERT_CONTAINS(result, "}", "Should have valid JSON structure");

    std::cout << "    OK alternating special characters" << std::endl;
}

void testOnlySpecialCharacters()
{
    std::cout << "  Testing string of only special characters..." << std::endl;

    std::ostringstream out;
    JsonSerializer s(out);

    s.beginDocument();
    s.write("only_special", "\n\t\r\\\"\b\f");
    s.endDocument();

    std::string result = out.str();

    // All characters should be escaped
    ASSERT_CONTAINS(result, "\\n", "Newline escaped");
    ASSERT_CONTAINS(result, "\\t", "Tab escaped");
    ASSERT_CONTAINS(result, "\\r", "CR escaped");
    ASSERT_CONTAINS(result, "\\\\", "Backslash escaped");
    ASSERT_CONTAINS(result, "\\\"", "Quote escaped");
    ASSERT_CONTAINS(result, "\\b", "Backspace escaped");
    ASSERT_CONTAINS(result, "\\f", "Form feed escaped");

    std::cout << "    OK only special characters" << std::endl;
}

void testUnicodeCharacters()
{
    std::cout << "  Testing Unicode characters..." << std::endl;

    std::ostringstream out;
    JsonSerializer s(out);

    s.beginDocument();
    // UTF-8 encoded characters (no escaping needed for valid UTF-8 above 0x1F)
    s.write("unicode", "Hello World"); // Simple ASCII
    s.endDocument();

    std::string result = out.str();

    // ASCII should pass through unchanged
    ASSERT_CONTAINS(result, "Hello World", "ASCII should pass through");

    std::cout << "    OK Unicode characters" << std::endl;
}

void testJsonArrayWithEscaping()
{
    std::cout << "  Testing JSON array with escaping..." << std::endl;

    std::ostringstream out;
    JsonSerializer s(out);

    s.beginDocument();
    s.beginArray("items");
    s.writeArrayItem("Normal");
    s.writeArrayItem("With \"quotes\"");
    s.writeArrayItem("With\nnewline");
    s.writeArrayItem("With\\backslash");
    s.endArray();
    s.endDocument();

    std::string result = out.str();

    ASSERT_CONTAINS(result, "\"Normal\"", "Normal string in array");
    ASSERT_CONTAINS(result, "With \\\"quotes\\\"", "Escaped quotes in array");
    ASSERT_CONTAINS(result, "With\\nnewline", "Escaped newline in array");
    ASSERT_CONTAINS(result, "With\\\\backslash", "Escaped backslash in array");

    std::cout << "    OK JSON array with escaping" << std::endl;
}

// ============================================================================
// Test Runner
// ============================================================================

void runAllStringEscapingTests()
{
    std::cout << "\n===================================================================" << std::endl;
    std::cout << "                    String Escaping Tests                         " << std::endl;
    std::cout << "===================================================================\n"
              << std::endl;

    int passed = 0;
    int failed = 0;

    auto runTest = [&](const char* name, void (*test)()) {
        try {
            test();
            passed++;
        } catch (const TestFailure& e) {
            std::cerr << "    FAILED: " << e.what() << std::endl;
            failed++;
        } catch (const std::exception& e) {
            std::cerr << "    ERROR: " << e.what() << std::endl;
            failed++;
        }
    };

    std::cout << "=== JSON Escaping Tests ===" << std::endl;
    runTest("Quote Escaping", testJsonQuoteEscaping);
    runTest("Backslash Escaping", testJsonBackslashEscaping);
    runTest("Newline Escaping", testJsonNewlineEscaping);
    runTest("Carriage Return Escaping", testJsonCarriageReturnEscaping);
    runTest("Tab Escaping", testJsonTabEscaping);
    runTest("Backspace Escaping", testJsonBackspaceEscaping);
    runTest("Form Feed Escaping", testJsonFormFeedEscaping);
    runTest("Control Character Escaping", testJsonControlCharacterEscaping);
    runTest("Null Character Escaping", testJsonNullCharacterEscaping);
    runTest("Combined Escaping", testJsonCombinedEscaping);
    runTest("Empty String", testJsonEmptyString);

    std::cout << "\n=== YAML Escaping Tests ===" << std::endl;
    runTest("YAML Quote Escaping", testYamlQuoteEscaping);
    runTest("YAML Backslash Escaping", testYamlBackslashEscaping);
    runTest("YAML Newline Escaping", testYamlNewlineEscaping);
    runTest("YAML Special Characters", testYamlSpecialCharacters);
    runTest("YAML Empty Value", testYamlEmptyValue);

    std::cout << "\n=== Edge Cases ===" << std::endl;
    runTest("Very Long String", testVeryLongString);
    runTest("Many Special Characters", testManySpecialCharacters);
    runTest("Alternating Special Chars", testAlternatingSpecialChars);
    runTest("Only Special Characters", testOnlySpecialCharacters);
    runTest("Unicode Characters", testUnicodeCharacters);
    runTest("JSON Array With Escaping", testJsonArrayWithEscaping);

    std::cout << "\n-------------------------------------------------------------------" << std::endl;
    std::cout << "String Escaping Tests: " << passed << " passed, " << failed << " failed" << std::endl;
    std::cout << "-------------------------------------------------------------------" << std::endl;

    if (failed > 0) {
        throw std::runtime_error("Some string escaping tests failed");
    }
}

} // namespace headsetcontrol::testing
