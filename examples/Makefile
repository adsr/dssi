all: dssi_example_host trivial_synth.so less_trivial_synth.so

test: all
	 DSSI_PATH=`pwd` ./dssi_example_host less_trivial_synth.so

CFLAGS = -Wall -g3 -I../dssi
LDFLAGS = -ljack -lasound

%.so: %.o
	gcc -nostartfiles -shared -lc -lm -o $*.so $*.o

dssi_example_host:	dssi_example_host.o

dssi_example_host.o:	../dssi/dssi.h
trivial_synth.o:	../dssi/dssi.h
