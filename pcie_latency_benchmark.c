/*
The MIT License (MIT)

Copyright (c) 2016 EnyxSA

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

#define MAX_READ_SIZE 255lu

struct cmd_line
{
    uint64_t iteration_count;
    uint64_t wait_time_us;
    uint64_t limit_ns;
    char * slot;
};

static void
print_usage(const char * name)
{
    fprintf(stderr, "usage: %s [options] PCIE_SLOT\n"
                    "where:\n"
                    "\t PCI_SLOT           The slot identifier of the card\n"
                    "\t                    e.g. 04:00.0\n"
                    "\n"
                    "\t -h                 Display this usage message\n"
                    "\t -i ITERATION       Define the read count\n"
                    "\t -w WAIT_TIME_US    Define the wait between reads\n"
                    "\t -l LIMIT_NS        Count the sample above this limit\n",
                    name);
}

static int
parse_cmd_line(int argc, char * argv[], struct cmd_line * cmd_line)
{
    int err = -1;

    cmd_line->iteration_count = 10 * 1000 * 1000;
    cmd_line->wait_time_us = 0;
    cmd_line->limit_ns = 0;

    int i;
    while ((i = getopt(argc, argv, "hi:w:l:")) != -1)
        switch(i)
        {
            default:
            case 'h':
                print_usage(argv[0]);
                goto arg_check_failed;
            case 'i':
                cmd_line->iteration_count = atoll(optarg);
                break;
            case 'w':
                cmd_line->wait_time_us = atoll(optarg);
                break;
            case 'l':
                cmd_line->limit_ns = atoll(optarg);
                break;
        }

    cmd_line->slot = argv[optind];
    if (! cmd_line->slot)
    {
        print_usage(argv[0]);
        goto arg_check_failed;
    }

    err = 0;

arg_check_failed:
    return err;
}

static struct pci_dev *
create_pci_dev(struct pci_access * pci, char * slot)
{
    struct pci_filter filter;
    pci_filter_init(pci, &filter);
    if (pci_filter_parse_slot(&filter, slot))
    {
        fprintf(stderr, "Failed to parse device id %s\n", slot);
        goto pci_filter_parse_failed;
    }

    pci_init(pci);

    struct pci_dev * dev = pci_get_dev(pci,
                                       filter.domain,
                                       filter.bus,
                                       filter.slot,
                                       filter.func);
    if (! dev)
    {
        fprintf(stderr, "Failed to allocate dev\n");
        goto pci_get_dev_failed;
    }

    pci_fill_info(dev, PCI_FILL_IDENT);

    return dev;

pci_get_dev_failed:
pci_filter_parse_failed:
    return NULL;
}

static void
print_results_above(uint64_t limit_ns,
                    double ns_per_cycle,
                    unsigned long * timestamps,
                    size_t iteration_count,
                    double max)
{
    double limit = limit_ns / ns_per_cycle;
    if (limit >= max)
        return;

    double range = max - limit;
    double delta = range / 5.;

    fprintf(stdout, "\n");

    for (; limit < max; limit += delta)
        fprintf(stdout, "Above %'.0lf ns: %'zu\n",
                limit * ns_per_cycle,
                above(limit, timestamps, iteration_count));
}

static void
print_results(double tsc_ghz,
              unsigned long * timestamps,
              size_t iteration_count,
              unsigned long test_duration_cycles,
              uint64_t limit_ns)
{
    double ns_per_cycle = 1. / tsc_ghz;

    size_t min_index = gsl_stats_ulong_min_index(timestamps, 1,
                                                 iteration_count);
    size_t max_index = gsl_stats_ulong_max_index(timestamps, 1,
                                                 iteration_count);
    double mean = gsl_stats_ulong_mean(timestamps, 1, iteration_count);
    double sd = gsl_stats_ulong_sd(timestamps, 1, iteration_count);

    fprintf(stdout, "Samples count: %'zu\n"
                    "Sampling duration: %'.0lf ms\n"
                    "Detected frequency: %.3lf GHz\n"
                    "\n"
                    "Min: %'.0lf ns @%zu\n"
                    "Mean: %'.0lf ns\n"
                    "Max: %'.0lf ns @%zu\n"
                    "\n"
                    "Std: %'.0lf ns\n",
            iteration_count,
            test_duration_cycles * ns_per_cycle / (1000. * 1000.),
            tsc_ghz,
            ns_per_cycle * timestamps[min_index], min_index,
            ns_per_cycle * mean,
            ns_per_cycle * timestamps[max_index], max_index,
            ns_per_cycle * sd);

    if (limit_ns)
        print_results_above(limit_ns,
                            ns_per_cycle,
                            timestamps,
                            iteration_count,
                            timestamps[max_index]);

}

static void
perform_reads(struct pci_dev * dev,
              unsigned long * timestamps,
              uint64_t iteration_count,
              uint64_t wait_time_us)
{
    size_t i;
    for (i = 0; i != iteration_count; ++i)
    {
        struct timestamp t;
        read_timestamp_counter(&t);

        pci_read_word(dev, PCI_VENDOR_ID);

        timestamps[i] = cycle_since_timestamp(&t);

        if (wait_time_us)
            usleep(wait_time_us);
    }
}

static void
print_device_name(struct pci_access * pci, struct pci_dev * dev)
{
    char device_name[1024];
    if (pci_lookup_name(pci, device_name, sizeof(device_name),
                        PCI_LOOKUP_VENDOR | PCI_LOOKUP_DEVICE,
                        dev->vendor_id, dev->device_id))
        fprintf(stdout, "Device: %s\n\n", device_name);
}

int
main(int argc, char* argv[])
{
    int err = EXIT_FAILURE;

    struct cmd_line cmd_line;

    setlocale(LC_ALL, "");

    if (parse_cmd_line(argc, argv, &cmd_line) < 0)
        goto arg_check_failed;

    struct pci_access * pci = pci_alloc();
    if (! pci)
        goto pci_alloc_failed;

    /* This access bypass the kernel and use a memory mapping
     * to PCI configuration registers */
    pci->method = PCI_ACCESS_I386_TYPE1;

    struct pci_dev * dev = create_pci_dev(pci, cmd_line.slot);
    if (! dev)
        goto create_pci_dev_failed;

    print_device_name(pci, dev);

    unsigned long * timestamps
            = malloc(sizeof(*timestamps) * cmd_line.iteration_count);
    if (! timestamps)
    {
        fprintf(stderr, "Can't allocate timestamp storage (%s)\n",
                strerror(errno));
        goto malloc_failed;
    }

    struct timestamp t;
    read_timestamp_counter(&t);

    perform_reads(dev,
                  timestamps,

                  cmd_line.iteration_count,
                  cmd_line.wait_time_us);

    unsigned long test_duration_cycles = cycle_since_timestamp(&t);

    double tsc_ghz = get_tsc_ghz();
    if (tsc_ghz == 0.) {
        fprintf(stderr, "Can't retrieve tsc frequency (%s)\n",
                strerror(errno));
        goto get_tsc_ghz_failed;
    }

    print_results(tsc_ghz,
                  timestamps,
                  cmd_line.iteration_count,
                  test_duration_cycles,
                  cmd_line.limit_ns);


    err = EXIT_SUCCESS;

get_tsc_ghz_failed:
    free(timestamps);
malloc_failed:
    pci_free_dev(dev);
create_pci_dev_failed:
    pci_cleanup(pci);
pci_alloc_failed:
arg_check_failed:
    return err;
}

