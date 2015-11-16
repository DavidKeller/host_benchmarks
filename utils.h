/*
The MIT License (MIT)

Copyright (c) [year] [fullname]

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#ifndef UTILS_H
#define UTILS_H

#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <inttypes.h>

struct timestamp {
    uint32_t coreId;
    uint64_t value;
};

#if defined(__GNUC__) && defined(__x86_64__)
static inline void
read_timestamp_counter(struct timestamp * t)
{
    uint64_t highTick, lowTick;

    asm volatile ("rdtscp" : "=d"(highTick), "=a"(lowTick), "=c"(t->coreId));

    t->value = highTick << 32 | lowTick;
}
#else
#    error Gnu compiler and x86_64 architecture is required.
#endif

/**
 *
 */
static inline uint64_t
diff_timestamps(const struct timestamp * before,
                const struct timestamp * after)
{
    assert(before->coreId == after->coreId);
    return after->value - before->value;
}

static inline uint64_t
cycle_since_timestamp(const struct timestamp * previous)
{
    struct timestamp now;
    read_timestamp_counter(&now);
    return diff_timestamps(previous, &now);
}

static inline double
get_cpu_mhz()
{
    double result = -1.;

    FILE * cpuinfo = fopen("/proc/cpuinfo", "r");
    if (! cpuinfo)
    {
        fprintf(stderr, "Failed to open /proc/cpuinfo (%s)",
                strerror(errno));
        goto fopen_failed;
    }

    char line[4096];
    while (fgets(line, sizeof(line), cpuinfo) &&
           sscanf(line, "cpu MHz : %lf", &result) == 0)
        continue;

    fclose(cpuinfo);
fopen_failed:
    return result;
}

static inline size_t
above(double limit,
      unsigned long * timestamps,
      size_t iteration_count)
{
    size_t count = 0;

    while (iteration_count)
        if (timestamps[--iteration_count] > limit)
            ++ count;

    return count;
}

#endif // UTILS_H
