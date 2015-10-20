#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <inttypes.h>
#include <locale.h>

#include <pci/pci.h>
#include <pci/header.h>
#include <gsl/gsl_statistics_ulong.h>

#include "utils.h"

struct cmd_line
{
    uint64_t iteration_count;
    uint64_t limit_ns;
};

static void
print_usage(const char * name)
{
    fprintf(stderr, "usage: %s [options] LIMIT_NS\n"
                    "where:\n"
                    "\t -h                 Display this usage message\n"
                    "\t -i ITERATION       Define the itration count\n",
                    name);
}

static int
parse_cmd_line(int argc, char * argv[], struct cmd_line * cmd_line)
{
    int err = -1;

    cmd_line->iteration_count = 10 * 1000 * 1000;

    int i;
    while ((i = getopt(argc, argv, "hi:")) != -1)
        switch(i)
        {
            default:
            case 'h':
                print_usage(argv[0]);
                goto arg_check_failed;
            case 'i':
                cmd_line->iteration_count = atoll(optarg);
                break;
        }

    if (! argv[optind])
    {
        print_usage(argv[0]);
        goto arg_check_failed;
    }

    cmd_line->limit_ns = atoll(argv[optind]);

    err = 0;

arg_check_failed:
    return err;
}

int
main(int argc, char* argv[])
{
    int err = EXIT_FAILURE;

    struct cmd_line cmd_line;

    setlocale(LC_ALL, "");

    if (parse_cmd_line(argc, argv, &cmd_line) < 0)
        goto arg_check_failed;

    const double cpu_mhz = get_cpu_mhz();
    if (cpu_mhz < 0)
        goto get_cpu_mhz_failed;

    const double cycles_per_ns = cpu_mhz / 1e3;
    const double cycles_limit = cycles_per_ns * cmd_line.limit_ns;

    size_t spikes_count = 0;

    struct timestamp before;
    read_timestamp_counter(&before);

    struct timestamp t[2];
    read_timestamp_counter(&t[0]);

    size_t i;
    for (i = 1; i < cmd_line.iteration_count; ++ i)
    {
        read_timestamp_counter(&t[ i % 2]);
        if (diff_timestamps(&t[(i - 1) % 2], &t[i % 2]) > cycles_limit)
            ++ spikes_count;
    }

    const double cycles_per_ms = cpu_mhz * 1e3;

    fprintf(stdout, "Iterations count: %'zu\n"
                    "Sampling duration: %'.0lf ms\n"
                    "Detected frequency: %.0lf Mhz\n"
                    "Spikes count: %'"PRIu64"\n",
                    cmd_line.iteration_count,
                    cycle_since_timestamp(&before) / cycles_per_ms,
                    cpu_mhz,
                    spikes_count);

    err = EXIT_SUCCESS;

get_cpu_mhz_failed:
arg_check_failed:
    return err;
}

