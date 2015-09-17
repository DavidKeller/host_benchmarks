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

#define MAX_READ_SIZE 255lu

struct cmd_line
{
    uint64_t iteration_count;
    uint64_t wait_time_us;
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
                    "\t -w WAIT_TIME_US    Define the wait between reads\n",
                    name);
}

static int
parse_cmd_line(int argc, char * argv[], struct cmd_line * cmd_line)
{
    int err = -1;

    cmd_line->iteration_count = 1000 * 1000;
    cmd_line->wait_time_us = 0;

    int i;
    while ((i = getopt(argc, argv, "hi:w:")) != -1)
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

    return dev;

pci_get_dev_failed:
pci_filter_parse_failed:
    return NULL;
}

static double
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

static size_t
above(unsigned long * timestamps,
      size_t iteration_count,
      double limit)
{
    size_t count = 0;

    while (iteration_count)
        if (timestamps[--iteration_count] > limit)
            ++ count;

    return count;
}

static void
print_results(unsigned long * timestamps,
              size_t iteration_count,
              unsigned long test_duration_cycles)
{
    size_t min_index = gsl_stats_ulong_min_index(timestamps, 1,
                                                 iteration_count);
    size_t max_index = gsl_stats_ulong_max_index(timestamps, 1,
                                                 iteration_count);
    double cpu_mhz = get_cpu_mhz();
    double ns_per_tick = 1000. / cpu_mhz;

    double mean = gsl_stats_ulong_mean(timestamps, 1, iteration_count);
    double sd = gsl_stats_ulong_sd(timestamps, 1, iteration_count);
    double limit = mean + 2 * sd;

    fprintf(stdout, "Samples count: %'zu\n"
                    "Sampling duration: %'.0lf ms\n"
                    "Detected frequency: %.0lf Mhz\n"
                    "\n"
                    "Min: %'.0lf ns @%zu\n"
                    "Mean: %'.0lf ns\n"
                    "Max: %'.0lf ns @%zu\n"
                    "\n"
                    "Std: %'.0lf ns\n"
                    "Above %'.0lf: %'zu\n",
            iteration_count,
            test_duration_cycles * ns_per_tick / (1000. * 1000.),
            cpu_mhz,
            ns_per_tick * timestamps[min_index], min_index,
            ns_per_tick * mean,
            ns_per_tick * timestamps[max_index], max_index,
            ns_per_tick * sd,
            ns_per_tick * limit,
            above(timestamps, iteration_count, limit));
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

    print_results(timestamps,
                  cmd_line.iteration_count,
                  test_duration_cycles);


    err = EXIT_SUCCESS;

    free(timestamps);
malloc_failed:
    pci_free_dev(dev);
create_pci_dev_failed:
    pci_cleanup(pci);
pci_alloc_failed:
arg_check_failed:
    return err;
}
