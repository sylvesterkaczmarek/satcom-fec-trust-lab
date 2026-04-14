#ifndef SATCOMFEC_TOOLS_JSON_OUTPUT_H
#define SATCOMFEC_TOOLS_JSON_OUTPUT_H

#include <iomanip>
#include <sstream>
#include <string>

namespace satcomfec::tools {

inline std::string escape_json(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (unsigned char ch : value) {
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
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}

}  // namespace satcomfec::tools

#endif  // SATCOMFEC_TOOLS_JSON_OUTPUT_H
