
PREFIX ?= /usr/local

TARGETS = \
	jack-dssi-host

all: $(TARGETS)

install: all
	mkdir -p $(PREFIX)/bin
	cp jack-dssi-host $(PREFIX)/bin/

CFLAGS = -Wall -g3 -I../dssi $(shell pkg-config liblo --cflags)
CXXFLAGS = $(CFLAGS)

LDLIBS = -ljack -lasound -lpthread $(shell pkg-config liblo --libs)

MB_O = ../message_buffer/message_buffer.o

jack-dssi-host:	jack-dssi-host.o $(MB_O)

jack-dssi-host.o:	../dssi/dssi.h jack-dssi-host.h

%.so: %.o ../dssi/dssi.h $(MB_O)
	gcc -nostartfiles -shared -lc -lm -o $*.so $*.o ../message_buffer/message_buffer.o

clean:
	rm -f *.o *.moc.*

distclean:	clean
	rm -f *~ $(TARGETS)


