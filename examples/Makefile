all: dssi_example_host trivial_synth.so

CFLAGS = -Wall -g3 -I../dssi
LDFLAGS = -ljack -lasound

dssi_example_host:	dssi_example_host.o

trivial_synth.so:	trivial_synth.o
	gcc -nostartfiles -shared -lc -lm -o trivial_synth.so trivial_synth.o

dssi_example_host.o:	../dssi/dssi.h
trivial_synth.o:	../dssi/dssi.h

