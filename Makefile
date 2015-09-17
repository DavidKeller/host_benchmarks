CFLAGS+=-O3 -Wall
LFLAGS+=-lpci -lgsl -lgslcblas -lm

pcie_latency_benchmark: main.c
	$(CC) $(CFLAGS) $(LFLAGS) -o $@ $^
