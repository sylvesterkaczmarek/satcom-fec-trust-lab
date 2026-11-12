#pragma once

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

namespace satcomfec::tools {

// Locate a top-level fixture field, skipping strings and nested metadata.
inline bool find_metadata_field(
    const std::string& text, const std::string& key, std::size_t& value_start) {
    std::size_t depth = 0;
    for (std::size_t pos = 0; pos < text.size(); ++pos) {
        const char ch = text[pos];
        if (ch == '{' || ch == '[') {
            ++depth;
        } else if (ch == '}' || ch == ']') {
            if (depth == 0) {
                return false;
            }
            --depth;
        } else if (ch == '"') {
            const auto begin = ++pos;
            while (pos < text.size() && text[pos] != '"') {
                if (text[pos] == '\\') {
                    ++pos;
                }
                ++pos;
            }
            if (depth == 1 && text.compare(begin, pos - begin, key) == 0) {
                const auto colon = text.find_first_not_of(" \t\r\n", pos + 1);
                if (colon != std::string::npos && text[colon] == ':') {
                    value_start = text.find_first_not_of(" \t\r\n", colon + 1);
                    return value_start != std::string::npos;
                }
            }
        }
    }
    return false;
}

inline bool json_value_end(const std::string& text, std::size_t position) {
    position = text.find_first_not_of(" \t\r\n", position);
    return position == std::string::npos || text[position] == ',' ||
           text[position] == ']' || text[position] == '}';
}

inline bool json_number_end(
    const std::string& text, std::size_t start, std::size_t& end) {
    std::size_t position = start;
    const auto digit = [&](std::size_t index) {
        return index < text.size() && text[index] >= '0' && text[index] <= '9';
    };
    if (position < text.size() && text[position] == '-') {
        ++position;
    }
    if (!digit(position)) {
        return false;
    }
    if (text[position] == '0') {
        ++position;
    } else {
        while (digit(position)) {
            ++position;
        }
    }
    if (position < text.size() && text[position] == '.') {
        ++position;
        if (!digit(position)) {
            return false;
        }
        while (digit(position)) {
            ++position;
        }
    }
    if (position < text.size() && (text[position] == 'e' || text[position] == 'E')) {
        ++position;
        if (position < text.size() && (text[position] == '+' || text[position] == '-')) {
            ++position;
        }
        if (!digit(position)) {
            return false;
        }
        while (digit(position)) {
            ++position;
        }
    }
    if (!json_value_end(text, position)) {
        return false;
    }
    end = position;
    return true;
}

inline bool parse_metadata_number(
    const std::string& text, std::size_t start, double& value,
    std::size_t* token_end = nullptr) {
    std::size_t end = 0;
    if (!json_number_end(text, start, end)) {
        return false;
    }
    const std::string token = text.substr(start, end - start);
    char* parsed_end = nullptr;
    errno = 0;
    const double parsed = std::strtod(token.c_str(), &parsed_end);
    if (parsed_end != token.c_str() + token.size() || errno == ERANGE ||
        !std::isfinite(parsed)) {
        return false;
    }
    value = parsed;
    if (token_end != nullptr) {
        *token_end = end;
    }
    return true;
}

inline bool parse_metadata_number_array(
    const std::string& json,
    std::size_t position,
    std::vector<double>& values) {
    if (position >= json.size() || json[position] != '[') {
        return false;
    }
    values.clear();
    ++position;
    while (true) {
        position = json.find_first_not_of(" \t\r\n", position);
        if (position == std::string::npos) {
            return false;
        }
        double parsed = 0.0;
        if (!parse_metadata_number(json, position, parsed, &position)) {
            return false;
        }
        values.push_back(parsed);
        position = json.find_first_not_of(" \t\r\n", position);
        if (position == std::string::npos) {
            return false;
        }
        if (json[position] == ']') {
            return json_value_end(json, position + 1);
        }
        if (json[position] != ',') {
            return false;
        }
        ++position;
    }
}

// Convert integer-valued JSON numbers exactly, including decimal/exponent forms.
// Going through double would round SIZE_MAX upward on 64-bit platforms.
inline bool parse_metadata_size(
    const std::string& text, std::size_t start, std::size_t& value) {
    std::size_t end = 0;
    if (!json_number_end(text, start, end)) {
        return false;
    }
    const bool negative = text[start] == '-';
    std::size_t position = start + (negative ? 1 : 0);
    std::string digits;
    std::size_t fractional_digits = 0;
    bool fractional = false;
    while (position < end && text[position] != 'e' && text[position] != 'E') {
        if (text[position] == '.') {
            fractional = true;
        } else {
            digits.push_back(text[position]);
            fractional_digits += fractional ? 1 : 0;
        }
        ++position;
    }
    const std::size_t first_nonzero = digits.find_first_not_of('0');
    if (first_nonzero == std::string::npos) {
        value = 0;
        return true;
    }
    if (negative) {
        return false;
    }
    digits.erase(0, first_nonzero);

    bool negative_exponent = false;
    std::size_t exponent = 0;
    if (position < end) {
        ++position;
        if (text[position] == '+' || text[position] == '-') {
            negative_exponent = text[position] == '-';
            ++position;
        }
        const std::size_t limit = text.size() + std::numeric_limits<std::size_t>::digits10 + 1;
        for (; position < end; ++position) {
            const std::size_t digit = static_cast<std::size_t>(text[position] - '0');
            if (exponent > (limit - digit) / 10) {
                return false;
            }
            exponent = exponent * 10 + digit;
        }
    }
    std::size_t remove = 0;
    std::size_t append = 0;
    if (negative_exponent) {
        if (exponent > digits.size() || fractional_digits > digits.size() - exponent) {
            return false;
        }
        remove = fractional_digits + exponent;
    } else if (exponent < fractional_digits) {
        remove = fractional_digits - exponent;
    } else {
        append = exponent - fractional_digits;
    }
    if (remove >= digits.size() ||
        (remove != 0 && digits.find_first_not_of('0', digits.size() - remove) !=
                            std::string::npos)) {
        return false;
    }
    digits.resize(digits.size() - remove);
    if (append > std::numeric_limits<std::size_t>::digits10 + 1 ||
        digits.size() + append > std::numeric_limits<std::size_t>::digits10 + 1) {
        return false;
    }
    digits.append(append, '0');
    std::size_t parsed = 0;
    for (const char character : digits) {
        const std::size_t digit = static_cast<std::size_t>(character - '0');
        if (parsed > (std::numeric_limits<std::size_t>::max() - digit) / 10) {
            return false;
        }
        parsed = parsed * 10 + digit;
    }
    value = parsed;
    return true;
}

}  // namespace satcomfec::tools
