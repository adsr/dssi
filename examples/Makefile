all: dssi_example_host trivial_synth.so less_trivial_synth.so dssi_osc_send \
	dssi_osc_update

CFLAGS = -Wall -g3 -O3 -I../dssi $(shell pkg-config liblo --cflags)
LDLIBS = -ljack -lasound -lpthread $(shell pkg-config liblo --libs)

dssi_example_host:	dssi_example_host.o message_buffer.o osc_url.o

dssi_osc_send:	dssi_osc_send.o message_buffer.o osc_url.o

dssi_osc_update:	dssi_osc_update.o message_buffer.o osc_url.o

dssi_example_host.o:	../dssi/dssi.h

%.so: %.o ../dssi/dssi.h message_buffer.o
	gcc -nostartfiles -shared -lc -lm -o $*.so $*.o message_buffer.o

clean:
	rm -f *.o *.so

test: all
	DSSI_PATH=`pwd` ./dssi_example_host less_trivial_synth.so
