Description
===========
This tool measures the latency between CPU
and PCIe components (e.g. endpoint, root complex)

Build requirements
==================
* libpci devel
* libgsl devel

Usage
=====
Poll device _01:00.0_ from _cpu3_ and show stats on sample:

    $taskset -c 3  pcie_latency_benchmark 01:00.0

Poll device _01:00.0_ from _cpu3_ and show samples _above 2ms RTT_:

    $taskset -c 3  pcie_latency_benchmark 01:00.0 -l 2000

