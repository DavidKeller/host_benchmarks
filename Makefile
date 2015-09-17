CFLAGS=-O3 -Wall
LIBS=-lpci -lgsl -lgslcblas -lm

OUT=pcie_latency_benchmark

$(OUT): main.o
	$(CC) $(CFLAGS) $(LIBS) -o $@ $^

.PHONY: clean

clean:
	rm -f *.o $(OUT)

