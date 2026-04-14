#ifndef SATCOMFEC_LOGGING_H
#define SATCOMFEC_LOGGING_H

#ifdef __ANDROID__
#include <android/log.h>
#else
#include <cstdio>
#endif

namespace satcomfec {

constexpr const char* kLogTag = "satcomfec";

inline void log_info(const char* message) {
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", message);
#else
    std::fprintf(stderr, "[%s][INFO] %s\n", kLogTag, message);
#endif
}

inline void log_error(const char* message) {
#ifdef __ANDROID__
    __android_log_print(ANDROID_LOG_ERROR, kLogTag, "%s", message);
#else
    std::fprintf(stderr, "[%s][ERROR] %s\n", kLogTag, message);
#endif
}

}  // namespace satcomfec

#endif  // SATCOMFEC_LOGGING_H
