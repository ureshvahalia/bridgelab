#include "log.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

static LogLevel currentLevel = LOG_INFO;

void
logSetLevel (LogLevel level)
{
    currentLevel = level;
}

LogLevel
logGetLevel (void)
{
    return currentLevel;
}

int
logSetLevelFromString (const char* s)
{
    char buf[16];
    size_t i;

    if (s == NULL)
        return 0;

    for (i = 0; i < sizeof (buf) - 1 && s[i] != '\0'; i++)
        buf[i] = (char) tolower ((unsigned char) s[i]);
    buf[i] = '\0';

    if (strcmp (buf, "error") == 0 || strcmp (buf, "0") == 0)
        currentLevel = LOG_ERROR;
    else if (strcmp (buf, "warning") == 0 || strcmp (buf, "warn") == 0 || strcmp (buf, "1") == 0)
        currentLevel = LOG_WARNING;
    else if (strcmp (buf, "info") == 0 || strcmp (buf, "2") == 0)
        currentLevel = LOG_INFO;
    else if (strcmp (buf, "debug") == 0 || strcmp (buf, "3") == 0)
        currentLevel = LOG_DEBUG;
    else
        return 0;

    return 1;
}

static void
logWrite (LogLevel level, const char* prefix, const char* fmt, va_list args)
{
    if (level > currentLevel)
        return;
    fprintf (stderr, "%s", prefix);
    vfprintf (stderr, fmt, args);
}

void
logError (const char* fmt, ...)
{
    va_list args;
    va_start (args, fmt);
    logWrite (LOG_ERROR, "[ERROR] ", fmt, args);
    va_end (args);
}

void
logWarning (const char* fmt, ...)
{
    va_list args;
    va_start (args, fmt);
    logWrite (LOG_WARNING, "[WARN] ", fmt, args);
    va_end (args);
}

void
logInfo (const char* fmt, ...)
{
    va_list args;
    va_start (args, fmt);
    logWrite (LOG_INFO, "[INFO] ", fmt, args);
    va_end (args);
}

void
logDebug (const char* fmt, ...)
{
    va_list args;
    va_start (args, fmt);
    logWrite (LOG_DEBUG, "[DEBUG] ", fmt, args);
    va_end (args);
}
