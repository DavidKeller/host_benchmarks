Description
===========
This package contains various tools used
to measure latencies. (e.g. CPU spikes, PCIe to CPU)

Build requirements
==================
* libpci devel
* libgsl devel

Usage
=====

pcie\_latency\_benchmark
======
Poll device _01:00.0_ from _cpu3_:

    $taskset -c 3  pcie_latency_benchmark 01:00.0

Poll device _01:00.0_ from _cpu3_ and show samples _above 2ms RTT_:

    $taskset -c 3  pcie_latency_benchmark 01:00.0 -l 2000

cpu\_spikes
======
Monitor spikes above 1000ns from _cpu3_:

    $taskset -c 3 cpu_spikes 1000

