#pragma once
/***
    Modern C++20 Argument Parser
    - Declarative option definition using builder pattern
    - Type-safe value handling with templates
    - Automatic getopt_long structure generation
    - Built-in validation (ranges, enums)
    - Clean error handling
***/

#include <algorithm>
#include <charconv>
#include <format>
#include <functional>
#include <getopt.h>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace cli {

// ============================================================================
// Error handling
// ============================================================================

struct ParseError {
    std::string message;
    std::string option_name;

    [[nodiscard]] std::string format() const
    {
        if (option_name.empty()) {
            return message;
        }
        return std::format("{}: {}", option_name, message);
    }
};

// ============================================================================
// Integer parsing helper
// ============================================================================

template <std::integral T>
[[nodiscard]] std::optional<ParseError> parseInteger(
    std::string_view arg, T& out, T min_val, T max_val, std::string_view option_name)
{
    long val       = 0;
    auto [ptr, ec] = std::from_chars(arg.data(), arg.data() + arg.size(), val);

    if (ec != std::errc {} || ptr != arg.data() + arg.size()) {
        return ParseError { std::format("invalid number '{}'", arg), std::string(option_name) };
    }

    if (val < static_cast<long>(min_val) || val > static_cast<long>(max_val)) {
        return ParseError {
            std::format("value {} out of range [{}, {}]", val, min_val, max_val),
            std::string(option_name)
        };
    }

    out = static_cast<T>(val);
    return std::nullopt;
}

// ============================================================================
// Option argument requirements
// ============================================================================

enum class ArgRequirement {
    None, // Flag only, no argument
    Required, // Must have argument
    Optional // Argument is optional
};

// ============================================================================
// Option handler - type-erased callable
// ============================================================================

class OptionHandler {
public:
    using HandlerFunc = std::function<std::optional<ParseError>(std::optional<std::string_view>)>;

    OptionHandler() = default;
    explicit OptionHandler(HandlerFunc func)
        : handler_(std::move(func))
    {
    }

    std::optional<ParseError> operator()(std::optional<std::string_view> arg) const
    {
        return handler_ ? handler_(arg) : std::nullopt;
    }

private:
    HandlerFunc handler_;
};

// ============================================================================
// Option specification
// ============================================================================

struct OptionSpec {
    std::string long_name;
    char short_name        = 0;
    ArgRequirement arg_req = ArgRequirement::None;
    std::string description;
    std::string value_hint; // e.g., "LEVEL", "NUMBER"
    OptionHandler handler;

    // For generating getopt_long structure
    [[nodiscard]] int getoptArgType() const
    {
        switch (arg_req) {
        case ArgRequirement::None:
            return no_argument;
        case ArgRequirement::Required:
            return required_argument;
        case ArgRequirement::Optional:
            return optional_argument;
        }
        return no_argument;
    }
};

// ============================================================================
// Argument Parser - Builder Pattern
// ============================================================================

class ArgumentParser {
public:
    explicit ArgumentParser(std::string_view program_name = "")
        : program_name_(program_name)
    {
    }

    // ========================================================================
    // Builder methods - return *this for chaining
    // ========================================================================

    // Simple flag (bool target, set to true when present)
    ArgumentParser& flag(char short_name, std::string_view long_name, bool& target,
        std::string_view description = "")
    {
        options_.push_back({
            .long_name   = std::string(long_name),
            .short_name  = short_name,
            .arg_req     = ArgRequirement::None,
            .description = std::string(description),
            .value_hint  = "",
            .handler     = OptionHandler([&target](auto) -> std::optional<ParseError> {
                target = true;
                return std::nullopt;
            }),
        });
        return *this;
    }

    // Integer value with range validation
    template <std::integral T>
    ArgumentParser& value(char short_name, std::string_view long_name,
        std::optional<T>& target, T min_val, T max_val,
        std::string_view description = "", std::string_view hint = "NUMBER")
    {
        options_.push_back({
            .long_name   = std::string(long_name),
            .short_name  = short_name,
            .arg_req     = ArgRequirement::Required,
            .description = std::string(description),
            .value_hint  = std::string(hint),
            .handler     = OptionHandler([&target, min_val, max_val, long_name](
                                         std::optional<std::string_view> arg) -> std::optional<ParseError> {
                if (!arg || arg->empty()) {
                    return ParseError { "requires a value", std::string(long_name) };
                }
                T parsed_value {};
                if (auto err = parseInteger(*arg, parsed_value, min_val, max_val, long_name)) {
                    return err;
                }
                target = parsed_value;
                return std::nullopt;
            }),
        });
        return *this;
    }

    // Boolean toggle (0/1 argument)
    ArgumentParser& toggle(char short_name, std::string_view long_name,
        std::optional<bool>& target, std::string_view description = "")
    {
        options_.push_back({
            .long_name   = std::string(long_name),
            .short_name  = short_name,
            .arg_req     = ArgRequirement::Required,
            .description = std::string(description),
            .value_hint  = "0|1",
            .handler     = OptionHandler([&target, long_name](
                                         std::optional<std::string_view> arg) -> std::optional<ParseError> {
                if (!arg || arg->empty()) {
                    return ParseError { "requires 0 or 1", std::string(long_name) };
                }

                if (*arg == "0") {
                    target = false;
                } else if (*arg == "1") {
                    target = true;
                } else {
                    return ParseError { std::format("expected 0 or 1, got '{}'", *arg), std::string(long_name) };
                }
                return std::nullopt;
            }),
        });
        return *this;
    }

    // Enum/choice value
    template <typename T>
    ArgumentParser& choice(char short_name, std::string_view long_name,
        T& target, const std::unordered_map<std::string, T>& choices,
        std::string_view description = "")
    {
        // Build hint from choices
        std::string hint;
        for (const auto& [name, _] : choices) {
            if (!hint.empty())
                hint += "|";
            hint += name;
        }

        options_.push_back({
            .long_name   = std::string(long_name),
            .short_name  = short_name,
            .arg_req     = ArgRequirement::Required,
            .description = std::string(description),
            .value_hint  = hint,
            .handler     = OptionHandler([&target, choices, long_name](
                                         std::optional<std::string_view> arg) -> std::optional<ParseError> {
                if (!arg || arg->empty()) {
                    return ParseError { "requires a value", std::string(long_name) };
                }

                std::string key(arg->data(), arg->size());
                // Case-insensitive lookup (cast to unsigned char to handle negative values safely)
                std::transform(key.begin(), key.end(), key.begin(),
                        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

                auto it = choices.find(key);
                if (it == choices.end()) {
                    return ParseError { std::format("unknown value '{}'", *arg), std::string(long_name) };
                }

                target = it->second;
                return std::nullopt;
            }),
        });
        return *this;
    }

    // Optional argument (flag that can optionally take a value)
    template <std::integral T>
    ArgumentParser& optional_value(char short_name, std::string_view long_name,
        bool& flag_target, T& value_target, T default_value, T min_val, T max_val,
        std::string_view description = "", std::string_view hint = "NUMBER")
    {
        options_.push_back({
            .long_name   = std::string(long_name),
            .short_name  = short_name,
            .arg_req     = ArgRequirement::Optional,
            .description = std::string(description),
            .value_hint  = std::string(hint),
            .handler     = OptionHandler([&flag_target, &value_target, default_value, min_val, max_val, long_name](
                                         std::optional<std::string_view> arg) -> std::optional<ParseError> {
                flag_target = true;

                if (!arg || arg->empty()) {
                    value_target = default_value;
                    return std::nullopt;
                }

                return parseInteger(*arg, value_target, min_val, max_val, long_name);
            }),
        });
        return *this;
    }

    // Custom handler for complex options
    ArgumentParser& custom(char short_name, std::string_view long_name,
        ArgRequirement arg_req,
        std::function<std::optional<ParseError>(std::optional<std::string_view>)> handler,
        std::string_view description = "", std::string_view hint = "")
    {
        options_.push_back({
            .long_name   = std::string(long_name),
            .short_name  = short_name,
            .arg_req     = arg_req,
            .description = std::string(description),
            .value_hint  = std::string(hint),
            .handler     = OptionHandler(std::move(handler)),
        });
        return *this;
    }

    // Long-only option (no short name)
    ArgumentParser& long_flag(std::string_view long_name, bool& target,
        std::string_view description = "")
    {
        return flag('\0', long_name, target, description);
    }

    template <std::integral T>
    ArgumentParser& long_value(std::string_view long_name,
        std::optional<T>& target, T min_val, T max_val,
        std::string_view description = "", std::string_view hint = "NUMBER")
    {
        return value('\0', long_name, target, min_val, max_val, description, hint);
    }

    ArgumentParser& long_toggle(std::string_view long_name,
        std::optional<bool>& target, std::string_view description = "")
    {
        return toggle('\0', long_name, target, description);
    }

    ArgumentParser& long_custom(std::string_view long_name,
        ArgRequirement arg_req,
        std::function<std::optional<ParseError>(std::optional<std::string_view>)> handler,
        std::string_view description = "", std::string_view hint = "")
    {
        return custom('\0', long_name, arg_req, std::move(handler), description, hint);
    }

    // ========================================================================
    // Parsing
    // ========================================================================

    [[nodiscard]] std::optional<ParseError> parse(int argc, char* argv[])
    {
        if (argc > 0 && program_name_.empty()) {
            program_name_ = argv[0];
        }

        // Build getopt structures
        auto [long_opts, short_opts, handler_map] = buildGetoptStructures();

        // Reset getopt state
        optind = 1;
        opterr = 0; // Suppress getopt error messages, we handle them

        int option_index = 0;
        int c;

        while ((c = getopt_long(argc, argv, short_opts.c_str(), long_opts.data(), &option_index)) != -1) {
            if (c == '?') {
                // Unknown option
                if (optopt != 0) {
                    return ParseError { std::format("unknown option '-{}'", static_cast<char>(optopt)), "" };
                }
                return ParseError { "unknown option", "" };
            }

            if (c == ':') {
                // Missing argument
                return ParseError { "missing required argument", "" };
            }

            // Find handler
            OptionHandler* handler = nullptr;
            std::string opt_name;

            if (c == 0) {
                // Long option
                opt_name = long_opts[option_index].name;
                auto it  = handler_map.find(opt_name);
                if (it != handler_map.end()) {
                    handler = it->second;
                }
            } else {
                // Short option
                auto it = handler_map.find(std::string(1, static_cast<char>(c)));
                if (it != handler_map.end()) {
                    handler = it->second;
                }
            }

            if (!handler) {
                return ParseError { "internal error: no handler", opt_name };
            }

            // Get argument, handling optional arguments properly
            std::optional<std::string_view> arg;
            if (optarg) {
                arg = optarg;
            } else {
                // Check for optional argument (getopt quirk: optarg is null but next argv might be the value)
                auto& spec = findSpec(c == 0 ? opt_name : std::string(1, static_cast<char>(c)));
                if (spec.arg_req == ArgRequirement::Optional && optind < argc && argv[optind][0] != '-') {
                    arg = argv[optind++];
                }
            }

            // Call handler
            if (auto error = (*handler)(arg)) {
                return error;
            }
        }

        // Collect positional arguments
        positional_args_.clear();
        for (int i = optind; i < argc; i++) {
            positional_args_.push_back(argv[i]);
        }

        return std::nullopt;
    }

    // ========================================================================
    // Accessors
    // ========================================================================

    [[nodiscard]] std::span<char* const> positionalArgs() const
    {
        return positional_args_;
    }

    [[nodiscard]] const std::string& programName() const
    {
        return program_name_;
    }

    [[nodiscard]] const std::vector<OptionSpec>& options() const
    {
        return options_;
    }

private:
    std::string program_name_;
    std::vector<OptionSpec> options_;
    std::vector<char*> positional_args_;

    struct GetoptStructures {
        std::vector<struct option> long_opts;
        std::string short_opts;
        std::unordered_map<std::string, OptionHandler*> handler_map;
    };

    [[nodiscard]] GetoptStructures buildGetoptStructures()
    {
        GetoptStructures result;
        result.short_opts = ":"; // Leading : for better error handling

        for (auto& spec : options_) {
            // Long option
            result.long_opts.emplace_back(option {
                spec.long_name.c_str(),
                spec.getoptArgType(),
                nullptr,
                spec.short_name ? spec.short_name : 0,
            });

            // Map long name to handler
            result.handler_map[spec.long_name] = &spec.handler;

            // Short option
            if (spec.short_name != '\0') {
                result.short_opts += spec.short_name;
                if (spec.arg_req == ArgRequirement::Required) {
                    result.short_opts += ':';
                } else if (spec.arg_req == ArgRequirement::Optional) {
                    result.short_opts += "::";
                }

                // Map short name to handler
                result.handler_map[std::string(1, spec.short_name)] = &spec.handler;
            }
        }

        // Terminator
        result.long_opts.emplace_back(option { nullptr, 0, nullptr, 0 });

        return result;
    }

    [[nodiscard]] const OptionSpec& findSpec(const std::string& name) const
    {
        for (const auto& spec : options_) {
            if (spec.long_name == name || (spec.short_name && std::string(1, spec.short_name) == name)) {
                return spec;
            }
        }
        // Should never happen
        static OptionSpec empty {};
        return empty;
    }
};

// ============================================================================
// Helper: Parse result wrapper for cleaner main() code
// ============================================================================

template <typename Options>
class ParseResultWrapper {
public:
    ParseResultWrapper(Options opts, std::optional<ParseError> error)
        : options_(std::move(opts))
        , error_(std::move(error))
    {
    }

    [[nodiscard]] bool hasError() const { return error_.has_value(); }
    [[nodiscard]] const ParseError& error() const { return *error_; }
    [[nodiscard]] Options& options() { return options_; }
    [[nodiscard]] const Options& options() const { return options_; }

    // Allow if (result) { use result.options() }
    explicit operator bool() const { return !hasError(); }

private:
    Options options_;
    std::optional<ParseError> error_;
};

} // namespace cli
