#ifndef SATCOMFEC_TOOLS_JSON_OUTPUT_H
#define SATCOMFEC_TOOLS_JSON_OUTPUT_H

#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>

namespace satcomfec::tools {

inline std::string escape_json(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (std::size_t index = 0; index < value.size(); ++index) {
        const unsigned char ch = static_cast<unsigned char>(value[index]);
        // Preserve valid UTF-8 names and text. Escaping each encoded byte as a
        // separate Unicode character changes the value when JSON is decoded.
        if (ch >= 0xC2 && ch <= 0xF4) {
            const std::size_t length = ch < 0xE0 ? 2 : (ch < 0xF0 ? 3 : 4);
            bool valid = length <= value.size() - index;
            for (std::size_t offset = 1; valid && offset < length; ++offset) {
                const auto continuation =
                    static_cast<unsigned char>(value[index + offset]);
                valid = continuation >= 0x80 && continuation <= 0xBF;
                if (offset == 1) {
                    valid = valid && !(ch == 0xE0 && continuation < 0xA0) &&
                            !(ch == 0xED && continuation >= 0xA0) &&
                            !(ch == 0xF0 && continuation < 0x90) &&
                            !(ch == 0xF4 && continuation >= 0x90);
                }
            }
            if (valid) {
                escaped.append(value, index, length);
                index += length - 1;
                continue;
            }
        }
        switch (static_cast<char>(ch)) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            case '\b':
                escaped += "\\b";
                break;
            case '\f':
                escaped += "\\f";
                break;
            default:
                if (ch < 0x20 || ch >= 0x7F) {
                    std::ostringstream control_escape;
                    control_escape.imbue(std::locale::classic());
                    control_escape << "\\u"
                                   << std::uppercase
                                   << std::hex
                                   << std::setw(4)
                                   << std::setfill('0')
                                   << static_cast<int>(ch);
                    escaped += control_escape.str();
                } else {
                    escaped += static_cast<char>(ch);
                }
                break;
        }
    }
    return escaped;
}

inline std::string format_float(double value, int precision = 6) {
    if (!std::isfinite(value)) {
        return "null";
    }
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}

}  // namespace satcomfec::tools

#endif  // SATCOMFEC_TOOLS_JSON_OUTPUT_H
