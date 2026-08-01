/* Leveled console logging: error/warning/info/debug, all to stderr.
   Level is controlled at runtime (see logSetLevel/logSetLevelFromString);
   no rebuild is needed to see debug traces. */

#ifndef LOG_H
#define LOG_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LOG_ERROR   = 0,
    LOG_WARNING = 1,
    LOG_INFO    = 2,
    LOG_DEBUG   = 3
} LogLevel;

void logSetLevel (LogLevel level);
LogLevel logGetLevel (void);

/* Accepts "error"/"warning"/"info"/"debug" (case-insensitive) or "0".."3".
   Returns 1 and applies the level on success, 0 on an unrecognized string
   (level is left unchanged). */
int logSetLevelFromString (const char* s);

#if defined(__GNUC__)
#define LOG_PRINTF_ATTR(fmtIdx, argIdx) __attribute__ ((format (printf, fmtIdx, argIdx)))
#else
#define LOG_PRINTF_ATTR(fmtIdx, argIdx)
#endif

void logError   (const char* fmt, ...) LOG_PRINTF_ATTR(1, 2);
void logWarning (const char* fmt, ...) LOG_PRINTF_ATTR(1, 2);
void logInfo    (const char* fmt, ...) LOG_PRINTF_ATTR(1, 2);
void logDebug   (const char* fmt, ...) LOG_PRINTF_ATTR(1, 2);

#ifdef __cplusplus
}
#endif

#endif
