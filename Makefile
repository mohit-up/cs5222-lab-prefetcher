
all: dpc2sim-stream dpc2sim-ghb

run: dpc2sim-stream
	zcat traces/mcf_trace2.dpc.gz | ./dpc2sim-stream

run_it: dpc2sim-ghb
	zcat traces/mcf_trace2.dpc.gz | ./dpc2sim-ghb

dpc2sim-stream:
	$(CXX) -fPIC -no-pie -Wall -o dpc2sim-stream example_prefetchers/stream_prefetcher.cc lib/dpc2sim.a

dpc2sim-ghb:
	$(CXX) -fPIC -no-pie -Wall -o dpc2sim-ghb example_prefetchers/ghb_prefetcher.cc lib/dpc2sim.a

clean:
	rm -rf dpc2sim-stream dpc2sim-ghb

.PHONY: all run clean
