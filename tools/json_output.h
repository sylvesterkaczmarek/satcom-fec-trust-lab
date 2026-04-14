#ifndef SATCOMFEC_TOOLS_JSON_OUTPUT_H
#define SATCOMFEC_TOOLS_JSON_OUTPUT_H

#include <iomanip>
#include <sstream>
#include <string>

namespace satcomfec::tools {

inline std::string escape_json(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            default:
                escaped += ch;
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
