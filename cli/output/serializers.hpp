#pragma once
/***
    Serializer Abstraction for HeadsetControl Output

    Provides a unified interface for serializing data to different formats
    (JSON, YAML, ENV, human-readable text).
***/

#include <format>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace headsetcontrol::serializers {

// ============================================================================
// Serializer Base Class
// ============================================================================

class Serializer {
public:
    virtual ~Serializer() = default;

    // Document structure
    virtual void beginDocument() = 0;
    virtual void endDocument()   = 0;

    // Object/Map structure
    virtual void beginObject(std::string_view name = "") = 0;
    virtual void endObject()                             = 0;

    // Array/List structure
    virtual void beginArray(std::string_view name) = 0;
    virtual void endArray()                        = 0;

    // Key-value pairs
    virtual void write(std::string_view key, std::string_view value) = 0;
    virtual void write(std::string_view key, int value)              = 0;
    virtual void write(std::string_view key, double value)           = 0;

    // Array items
    virtual void writeArrayItem(std::string_view value) = 0;
    virtual void writeArrayItem(int value)              = 0;
    virtual void writeArrayItem(double value)           = 0;

    // Convenience: optional values (only writes if has_value)
    template <typename T>
    void writeOptional(std::string_view key, const std::optional<T>& value)
    {
        if (value.has_value()) {
            write(key, *value);
        }
    }

    // Convenience: write array from vector
    template <typename T>
    void writeArray(std::string_view name, const std::vector<T>& items)
    {
        beginArray(name);
        for (const auto& item : items) {
            writeArrayItem(item);
        }
        endArray();
    }
};

// ============================================================================
// JSON Serializer
// ============================================================================

class JsonSerializer final : public Serializer {
public:
    explicit JsonSerializer(std::ostream& out = std::cout)
        : out_(out)
    {
    }

    void beginDocument() override
    {
        out_ << "{\n";
        indent_++;
    }

    void endDocument() override
    {
        out_ << "\n}\n";
    }

    void beginObject(std::string_view name) override
    {
        writeComma();
        writeIndent();
        if (!name.empty()) {
            out_ << std::format("\"{}\": ", name);
        }
        out_ << "{\n";
        indent_++;
        needs_comma_ = false;
    }

    void endObject() override
    {
        indent_--;
        out_ << "\n";
        writeIndent();
        out_ << "}";
        needs_comma_ = true;
    }

    void beginArray(std::string_view name) override
    {
        writeComma();
        writeIndent();
        out_ << std::format("\"{}\": [\n", name);
        indent_++;
        needs_comma_ = false;
    }

    void endArray() override
    {
        indent_--;
        out_ << "\n";
        writeIndent();
        out_ << "]";
        needs_comma_ = true;
    }

    void write(std::string_view key, std::string_view value) override
    {
        writeComma();
        writeIndent();
        out_ << std::format("\"{}\": \"{}\"", key, escape(value));
        needs_comma_ = true;
    }

    void write(std::string_view key, int value) override
    {
        writeComma();
        writeIndent();
        out_ << std::format("\"{}\": {}", key, value);
        needs_comma_ = true;
    }

    void write(std::string_view key, double value) override
    {
        writeComma();
        writeIndent();
        out_ << std::format("\"{}\": {}", key, value);
        needs_comma_ = true;
    }

    void writeArrayItem(std::string_view value) override
    {
        writeComma();
        writeIndent();
        out_ << std::format("\"{}\"", escape(value));
        needs_comma_ = true;
    }

    void writeArrayItem(int value) override
    {
        writeComma();
        writeIndent();
        out_ << value;
        needs_comma_ = true;
    }

    void writeArrayItem(double value) override
    {
        writeComma();
        writeIndent();
        out_ << std::format("{:.1f}", value);
        needs_comma_ = true;
    }

private:
    std::ostream& out_;
    size_t indent_    = 0;
    bool needs_comma_ = false;

    void writeIndent()
    {
        out_ << std::string(indent_ * 2, ' ');
    }

    void writeComma()
    {
        if (needs_comma_)
            out_ << ",\n";
    }

    /**
     * @brief Escape string for JSON output
     *
     * Handles all JSON special characters including control characters.
     * Compliant with RFC 8259.
     */
    [[nodiscard]] static std::string escape(std::string_view s)
    {
        std::string result;
        result.reserve(s.size() * 2); // Reserve extra space for escapes
        for (unsigned char c : s) {
            switch (c) {
            case '"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\b':
                result += "\\b";
                break;
            case '\f':
                result += "\\f";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                // Escape control characters (0x00-0x1F)
                if (c < 0x20) {
                    result += std::format("\\u{:04x}", static_cast<unsigned int>(c));
                } else {
                    result += static_cast<char>(c);
                }
            }
        }
        return result;
    }
};

// ============================================================================
// YAML Serializer
// ============================================================================

class YamlSerializer final : public Serializer {
public:
    explicit YamlSerializer(std::ostream& out = std::cout)
        : out_(out)
    {
    }

    void beginDocument() override
    {
        out_ << "---\n";
    }

    // YAML documents end implicitly - no closing syntax needed
    void endDocument() override { }

    void beginObject(std::string_view name) override
    {
        if (!name.empty()) {
            writeKey(name);
            out_ << ":\n";
            indent_++;
        } else if (array_object_depth_ > 0) {
            // Anonymous object inside another array object - just track depth
            array_object_depth_++;
            first_in_array_object_ = true;
        } else if (in_array_) {
            // Anonymous object in array - first write will use "- key:" format
            first_in_array_object_ = true;
            array_object_depth_    = 1;
        } else {
            indent_++;
        }
    }

    void endObject() override
    {
        if (array_object_depth_ > 0) {
            array_object_depth_--;
        } else {
            indent_--;
        }
        first_in_array_object_ = false;
    }

    void beginArray(std::string_view name) override
    {
        writeKey(name);
        out_ << ":\n";
        indent_++;
        array_depth_++;
        in_array_ = true;
    }

    void endArray() override
    {
        indent_--;
        array_depth_--;
        if (array_depth_ == 0) {
            in_array_ = false;
        }
        first_in_array_object_ = false;
    }

    void write(std::string_view key, std::string_view value) override
    {
        writeKey(key);
        if (value.empty()) {
            out_ << ":\n";
        } else {
            out_ << std::format(": \"{}\"\n", escapeYaml(value));
        }
    }

    void write(std::string_view key, int value) override
    {
        writeKey(key);
        out_ << std::format(": {}\n", value);
    }

    void write(std::string_view key, double value) override
    {
        writeKey(key);
        out_ << std::format(": {}\n", value);
    }

    void writeArrayItem(std::string_view value) override
    {
        writeIndent();
        out_ << std::format("- {}\n", value);
    }

    void writeArrayItem(int value) override
    {
        writeIndent();
        out_ << std::format("- {}\n", value);
    }

    void writeArrayItem(double value) override
    {
        writeIndent();
        out_ << std::format("- {:.1f}\n", value);
    }

    // YAML-specific: list item with key (for "- key: value" syntax)
    void writeListItem(std::string_view key, std::string_view value)
    {
        writeIndent();
        out_ << std::format("- {}: \"{}\"\n", formatKey(key), value);
    }

    void pushIndent(int extra = 1) { indent_ += extra; }
    void popIndent(int extra = 1) { indent_ -= extra; }

    void setInArray(bool value) { in_array_ = value; }

private:
    std::ostream& out_;
    size_t indent_              = 0;
    size_t array_depth_         = 0;
    size_t array_object_depth_  = 0; // Depth of anonymous objects in arrays
    bool in_array_              = false;
    bool first_in_array_object_ = false;

    void writeIndent()
    {
        out_ << std::string(indent_ * 2, ' ');
    }

    // Write a key with proper formatting for array context
    void writeKey(std::string_view key)
    {
        if (first_in_array_object_) {
            // First key in array object: "  - key" format
            // Indent before the dash, then write "- key"
            writeIndent();
            out_ << std::format("- {}", formatKey(key));
            first_in_array_object_ = false;
        } else if (array_object_depth_ > 0) {
            // Subsequent keys in array object: need extra 2 spaces to align with first key
            // because the "- " takes 2 chars that we need to account for
            writeIndent();
            out_ << std::format("  {}", formatKey(key));
        } else {
            writeIndent();
            out_ << formatKey(key);
        }
    }

    [[nodiscard]] static std::string formatKey(std::string_view key)
    {
        std::string result;
        result.reserve(key.size());
        for (size_t i = 0; i < key.size(); ++i) {
            char c = key[i];
            if (c == ' ' && !(i == 1 && key[0] == '-')) {
                result += '-';
            } else {
                result += c;
            }
        }
        return result;
    }

    /**
     * @brief Escape string for YAML output
     *
     * Handles YAML special characters that could break parsing.
     */
    [[nodiscard]] static std::string escapeYaml(std::string_view s)
    {
        std::string result;
        result.reserve(s.size() * 2);
        for (char c : s) {
            switch (c) {
            case '"':
                result += "\\\"";
                break;
            case '\\':
                result += "\\\\";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                result += c;
            }
        }
        return result;
    }
};

// ============================================================================
// ENV Serializer (Shell Environment Variables)
// ============================================================================

class EnvSerializer final : public Serializer {
public:
    explicit EnvSerializer(std::ostream& out = std::cout)
        : out_(out)
    {
    }

    // ENV format is flat key=value pairs - no document/object/array structure
    void beginDocument() override { }
    void endDocument() override { }
    void beginObject(std::string_view) override { }
    void endObject() override { }
    void beginArray(std::string_view) override { }
    void endArray() override { }

    void write(std::string_view key, std::string_view value) override
    {
        out_ << std::format("{}=\"{}\"\n", formatKey(key), value);
    }

    void write(std::string_view key, int value) override
    {
        out_ << std::format("{}={}\n", formatKey(key), value);
    }

    void write(std::string_view key, double value) override
    {
        out_ << std::format("{}={}\n", formatKey(key), value);
    }

    // Array items not supported in ENV format - use indexed keys instead
    void writeArrayItem(std::string_view) override { }
    void writeArrayItem(int) override { }
    void writeArrayItem(double) override { }

private:
    std::ostream& out_;

    [[nodiscard]] static std::string formatKey(std::string_view key)
    {
        std::string result;
        result.reserve(key.size());
        for (char c : key) {
            if (c == ' ' || c == '-') {
                result += '_';
            } else {
                result += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
        }
        return result;
    }
};

// ============================================================================
// Text Serializer (Human-Readable Output)
// ============================================================================

class TextSerializer final : public Serializer {
public:
    explicit TextSerializer(std::ostream& out = std::cout)
        : out_(out)
    {
    }

    void beginDocument() override { }
    void endDocument() override { }
    void beginObject(std::string_view) override { }
    void endObject() override { }
    void beginArray(std::string_view) override { }
    void endArray() override { }
    void write(std::string_view, std::string_view) override { }
    void write(std::string_view, int) override { }
    void write(std::string_view, double) override { }
    void writeArrayItem(std::string_view) override { }
    void writeArrayItem(int) override { }
    void writeArrayItem(double) override { }

    // Human-readable specific methods
    void printLine(std::string_view text) { out_ << text << '\n'; }

    template <typename... Args>
    void print(std::format_string<Args...> fmt, Args&&... args)
    {
        out_ << std::format(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void println(std::format_string<Args...> fmt, Args&&... args)
    {
        out_ << std::format(fmt, std::forward<Args>(args)...) << '\n';
    }

    std::ostream& stream() { return out_; }

private:
    std::ostream& out_;
};

// ============================================================================
// Short Serializer (Legacy Compact Output)
// ============================================================================

class ShortSerializer final : public Serializer {
public:
    explicit ShortSerializer(std::ostream& out = std::cout, std::ostream& err = std::cerr)
        : out_(out)
        , err_(err)
    {
    }

    void beginDocument() override { }

    void endDocument() override
    {
        out_.flush();
        err_.flush();
        err_ << "\nWarning: short output deprecated, use the -o option instead\n";
    }

    void beginObject(std::string_view) override { }
    void endObject() override { }
    void beginArray(std::string_view) override { }
    void endArray() override { }
    void write(std::string_view, std::string_view) override { }
    void write(std::string_view, int) override { }
    void write(std::string_view, double) override { }
    void writeArrayItem(std::string_view) override { }
    void writeArrayItem(int) override { }
    void writeArrayItem(double) override { }

    // Short output specific
    void printValue(int value) { out_ << value; }
    void printValue(std::string_view value) { out_ << value; }
    void printChar(char c)
    {
        if (c != '\0')
            out_ << c;
    }
    void printError(std::string_view source, std::string_view message)
    {
        err_ << std::format("Error: [{}]: {}\n", source, message);
    }
    void printWarning(std::string_view message) { err_ << message; }

private:
    std::ostream& out_;
    std::ostream& err_;
};

} // namespace headsetcontrol::serializers
