all: dssi_example_host trivial_synth.so less_trivial_synth.so

CFLAGS = -Wall -g3 -O3 -I../dssi
LDFLAGS = -ljack -lasound

dssi_example_host:	dssi_example_host.o

dssi_example_host.o:	../dssi/dssi.h

%.so: %.o ../dssi/dssi.h
	gcc -nostartfiles -shared -lc -lm -o $*.so $*.o

clean:
	rm -f *.o *.so

test: all
	DSSI_PATH=`pwd` ./dssi_example_host less_trivial_synth.so

