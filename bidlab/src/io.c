#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "log.h"
extern time_t start_time;

void
print_time_estimate (time_t elapsed, time_t remaining)
{
    if (elapsed < 120)
        logInfo ("Elapsed time %lld seconds, ", (long long)elapsed);
    else if (elapsed < 3660)
        logInfo ("Elapsed time %lld minutes, ", (long long)(elapsed / 60));
    else
        logInfo ("Elapsed time %lld hours, %lld minutes, ", (long long)(elapsed / 3600), (long long)((elapsed % 3600) / 60));
    if (remaining < 120)
        logInfo ("about %lld seconds to go\n", (long long)remaining);
    else if (remaining < 3660)
        logInfo ("about %lld minutes to go\n", (long long)(remaining / 60));
    else
        logInfo ("about %lld hours, %lld minutes to go\n", (long long)(remaining / 3600), (long long)((remaining % 3600) / 60));
}
