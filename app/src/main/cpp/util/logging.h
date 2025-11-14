#ifndef SATCOMFEC_LOGGING_H
#define SATCOMFEC_LOGGING_H

#include <android/log.h>

namespace satcomfec {

constexpr const char* kLogTag = "satcomfec";

inline void log_info(const char* message) {
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", message);
}

inline void log_error(const char* message) {
    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "%s", message);
}

}  // namespace satcomfec

#endif  // SATCOMFEC_LOGGING_H
